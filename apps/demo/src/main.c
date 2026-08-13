#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "microfx/scene.h"
#include "microfx/script.h"
#include "microfx/identity.h"
#include "microfx/sdf_renderer.h"
#include "microfx/quad_renderer.h"
#include "microfx/mesh_renderer.h"
#include "microfx/text_renderer.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DESIGN_WIDTH 1920
#define DESIGN_HEIGHT 1080
#define DEFAULT_OUTPUT_WIDTH 1280
#define DEFAULT_OUTPUT_HEIGHT 720
typedef struct {
    int outputWidth;
    int outputHeight;
    int nativeWidth;
    int nativeHeight;
    int targetFps;
    float pixelDensity;
    float minimumPixelDensity;
    bool automaticDensity;
    bool resolutionFixed;
} RuntimeConfig;

static void ApplyConfigFile(RuntimeConfig *config, const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) return;
    char line[160];
    while (fgets(line, sizeof(line), file) != NULL) {
        char key[80] = { 0 };
        char value[80] = { 0 };
        if (sscanf(line, " %79[^=]=%79s", key, value) != 2) continue;
        if (strcmp(key, "MICROFX_OUTPUT_WIDTH") == 0) config->outputWidth = atoi(value);
        else if (strcmp(key, "MICROFX_OUTPUT_HEIGHT") == 0) config->outputHeight = atoi(value);
        else if (strcmp(key, "MICROFX_TARGET_FPS") == 0) config->targetFps = atoi(value);
        else if (strcmp(key, "MICROFX_MIN_PIXEL_DENSITY") == 0) config->minimumPixelDensity = strtof(value, NULL);
        else if (strcmp(key, "MICROFX_PIXEL_DENSITY") == 0) {
            config->automaticDensity = (strcmp(value, "auto") == 0);
            if (!config->automaticDensity) config->pixelDensity = strtof(value, NULL);
        }
    }
    fclose(file);
}

static void ApplyEnvironment(RuntimeConfig *config)
{
    const char *value = getenv("MICROFX_OUTPUT_WIDTH");
    if (value != NULL) config->outputWidth = atoi(value);
    value = getenv("MICROFX_OUTPUT_HEIGHT");
    if (value != NULL) config->outputHeight = atoi(value);
    value = getenv("MICROFX_TARGET_FPS");
    if (value != NULL) config->targetFps = atoi(value);
    value = getenv("MICROFX_MIN_PIXEL_DENSITY");
    if (value != NULL) config->minimumPixelDensity = strtof(value, NULL);
    value = getenv("MICROFX_PIXEL_DENSITY");
    if (value != NULL) {
        config->automaticDensity = (strcmp(value, "auto") == 0);
        if (!config->automaticDensity) config->pixelDensity = strtof(value, NULL);
    }
    value = getenv("MICROFX_AUTO_DENSITY_VALUE");
    if (value != NULL && config->automaticDensity) config->pixelDensity = strtof(value, NULL);
}

static bool ReadDrmNativeMode(int *width, int *height)
{
    const char *modeFiles[] = {
        "/sys/class/drm/card1-HDMI-A-1/modes",
        "/sys/class/drm/card0-HDMI-A-1/modes",
        "/sys/class/drm/card1-HDMI-A-2/modes",
        "/sys/class/drm/card0-HDMI-A-2/modes"
    };
    for (unsigned int i = 0; i < sizeof(modeFiles)/sizeof(modeFiles[0]); i++) {
        FILE *file = fopen(modeFiles[i], "r");
        if (file == NULL) continue;
        int candidateWidth = 0;
        int candidateHeight = 0;
        int matched = fscanf(file, "%dx%d", &candidateWidth, &candidateHeight);
        fclose(file);
        if (matched == 2 && candidateWidth >= 320 && candidateHeight >= 240) {
            *width = candidateWidth;
            *height = candidateHeight;
            return true;
        }
    }
    return false;
}

static bool ReadClosestDrmMode(int targetWidth, int targetHeight,
                               int *selectedWidth, int *selectedHeight)
{
    const char *modeFiles[] = {
        "/sys/class/drm/card1-HDMI-A-1/modes",
        "/sys/class/drm/card0-HDMI-A-1/modes",
        "/sys/class/drm/card1-HDMI-A-2/modes",
        "/sys/class/drm/card0-HDMI-A-2/modes"
    };
    float bestScore = 1.0e9f;
    bool found = false;
    for (unsigned int fileIndex = 0;
         fileIndex < sizeof(modeFiles)/sizeof(modeFiles[0]); fileIndex++) {
        FILE *file = fopen(modeFiles[fileIndex], "r");
        if (file == NULL) continue;
        char line[80];
        while (fgets(line, sizeof(line), file) != NULL) {
            int width = 0;
            int height = 0;
            if (sscanf(line, "%dx%d", &width, &height) != 2) continue;
            if (width < 320 || height < 240) continue;
            float targetArea = (float)targetWidth*targetHeight;
            float candidateArea = (float)width*height;
            float targetAspect = (float)targetWidth/targetHeight;
            float candidateAspect = (float)width/height;
            float score = fabsf(logf(candidateArea/targetArea));
            score += fabsf(candidateAspect - targetAspect)*4.0f;
            if (candidateArea > targetArea) score += 0.025f;
            if (score < bestScore) {
                bestScore = score;
                *selectedWidth = width;
                *selectedHeight = height;
                found = true;
            }
        }
        fclose(file);
        if (found) break;
    }
    return found;
}

static RuntimeConfig LoadRuntimeConfig(const MicroFxScene *scene)
{
    RuntimeConfig config = {
        .outputWidth = 0,
        .outputHeight = 0,
        .nativeWidth = 0,
        .nativeHeight = 0,
        .targetFps = 30,
        .pixelDensity = 1.0f,
        .minimumPixelDensity = 0.5f,
        .automaticDensity = true,
        .resolutionFixed = false
    };
    ApplyConfigFile(&config, "/etc/microfx.conf");
    ApplyConfigFile(&config, "/data/config/microfx.conf");
    if (scene->runtime.configured) {
        config.outputWidth = scene->runtime.outputWidth;
        config.outputHeight = scene->runtime.outputHeight;
        config.targetFps = scene->runtime.targetFps;
        config.pixelDensity = scene->runtime.pixelDensity;
        config.minimumPixelDensity = scene->runtime.minimumPixelDensity;
        config.automaticDensity = scene->runtime.automaticDensity;
    }
    ApplyEnvironment(&config);

    config.resolutionFixed = (config.outputWidth > 0 && config.outputHeight > 0);
    if (!config.resolutionFixed) {
        if (!ReadDrmNativeMode(&config.nativeWidth, &config.nativeHeight)) {
            config.nativeWidth = DEFAULT_OUTPUT_WIDTH;
            config.nativeHeight = DEFAULT_OUTPUT_HEIGHT;
        }
        config.outputWidth = config.nativeWidth;
        config.outputHeight = config.nativeHeight;
    } else {
        config.nativeWidth = config.outputWidth;
        config.nativeHeight = config.outputHeight;
    }
    config.targetFps = (config.targetFps < 1) ? 30 : config.targetFps;
    config.minimumPixelDensity = Clamp(config.minimumPixelDensity, 0.25f, 1.0f);
    config.pixelDensity = Clamp(config.pixelDensity, config.minimumPixelDensity, 1.0f);
    if (config.resolutionFixed) {
        config.automaticDensity = false;
        config.pixelDensity = 1.0f;
    } else if (config.pixelDensity < 0.999f) {
        int targetWidth = (int)roundf(config.nativeWidth*config.pixelDensity);
        int targetHeight = (int)roundf(config.nativeHeight*config.pixelDensity);
        int selectedWidth = targetWidth;
        int selectedHeight = targetHeight;
        if (ReadClosestDrmMode(targetWidth, targetHeight,
                               &selectedWidth, &selectedHeight)) {
            config.outputWidth = selectedWidth;
            config.outputHeight = selectedHeight;
        } else {
            config.outputWidth = targetWidth;
            config.outputHeight = targetHeight;
        }
    }
    return config;
}

static bool FindNextLowerDrmMode(const RuntimeConfig *config,
                                 int currentWidth, int currentHeight,
                                 float *selectedDensity,
                                 int *selectedWidth, int *selectedHeight)
{
    float candidate = config->pixelDensity;
    while (candidate > config->minimumPixelDensity + 0.001f) {
        candidate = fmaxf(candidate - 0.025f, config->minimumPixelDensity);
        int targetWidth = (int)roundf(config->nativeWidth*candidate);
        int targetHeight = (int)roundf(config->nativeHeight*candidate);
        int width = targetWidth;
        int height = targetHeight;
        ReadClosestDrmMode(targetWidth, targetHeight, &width, &height);

        // Density is an ergonomic control, but DRM exposes a discrete mode
        // list. Skip density values that resolve to the mode already in use.
        if (width != currentWidth || height != currentHeight) {
            *selectedDensity = candidate;
            *selectedWidth = width;
            *selectedHeight = height;
            return true;
        }
        if (candidate <= config->minimumPixelDensity + 0.001f) break;
    }
    return false;
}

#if 0
// Removed private C showcase renderer. Kept inside this patch temporarily so
// the functional migration can be hardware-verified before its mechanical
// source deletion; no code in this block is compiled.
static GLuint CompileSceneShader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = { 0 };
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "GPU_SCENE shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int MeshTriangleVertexCount(const Mesh *mesh)
{
    return (mesh->indices != NULL) ? mesh->triangleCount*3 : mesh->vertexCount;
}

static void AppendMeshVertices(SceneVertex *vertices, int *cursor,
                               const Mesh *mesh, int objectId, Vector3 center)
{
    int count = MeshTriangleVertexCount(mesh);
    for (int i = 0; i < count; i++) {
        int source = (mesh->indices != NULL) ? mesh->indices[i] : i;
        SceneVertex *out = &vertices[(*cursor)++];
        out->position[0] = mesh->vertices[source*3 + 0] - center.x;
        out->position[1] = mesh->vertices[source*3 + 1] - center.y;
        out->position[2] = mesh->vertices[source*3 + 2] - center.z;
        if (mesh->normals != NULL) {
            out->normal[0] = mesh->normals[source*3 + 0];
            out->normal[1] = mesh->normals[source*3 + 1];
            out->normal[2] = mesh->normals[source*3 + 2];
        } else {
            out->normal[0] = 0.0f;
            out->normal[1] = 1.0f;
            out->normal[2] = 0.0f;
        }
        out->objectId = (float)objectId;
    }
}

static void AppendCubeVertex(SceneVertex *vertices, int *cursor, int objectId,
                             float x, float y, float z,
                             float nx, float ny, float nz)
{
    SceneVertex *out = &vertices[(*cursor)++];
    out->position[0] = x; out->position[1] = y; out->position[2] = z;
    out->normal[0] = nx; out->normal[1] = ny; out->normal[2] = nz;
    out->objectId = (float)objectId;
}

static void AppendCanonicalCube(SceneVertex *vertices, int *cursor, int objectId)
{
    static const float faces[6][4][3] = {
        {{-.5f,-.5f, .5f},{ .5f,-.5f, .5f},{ .5f, .5f, .5f},{-.5f, .5f, .5f}},
        {{ .5f,-.5f,-.5f},{-.5f,-.5f,-.5f},{-.5f, .5f,-.5f},{ .5f, .5f,-.5f}},
        {{ .5f,-.5f, .5f},{ .5f,-.5f,-.5f},{ .5f, .5f,-.5f},{ .5f, .5f, .5f}},
        {{-.5f,-.5f,-.5f},{-.5f,-.5f, .5f},{-.5f, .5f, .5f},{-.5f, .5f,-.5f}},
        {{-.5f, .5f, .5f},{ .5f, .5f, .5f},{ .5f, .5f,-.5f},{-.5f, .5f,-.5f}},
        {{-.5f,-.5f,-.5f},{ .5f,-.5f,-.5f},{ .5f,-.5f, .5f},{-.5f,-.5f, .5f}}
    };
    static const float normals[6][3] = {
        {0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}
    };
    static const int order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int face = 0; face < 6; face++) {
        for (int vertex = 0; vertex < 6; vertex++) {
            const float *position = faces[face][order[vertex]];
            AppendCubeVertex(vertices, cursor, objectId,
                             position[0], position[1], position[2],
                             normals[face][0], normals[face][1], normals[face][2]);
        }
    }
}

static void AppendEdgeVertex(SceneVertex *vertices, int *cursor, int objectId,
                             int faceAxis, int uAxis, int vAxis, float face,
                             float u, float v)
{
    SceneVertex *out = &vertices[(*cursor)++];
    out->position[faceAxis] = face;
    out->position[uAxis] = u;
    out->position[vAxis] = v;
    out->normal[faceAxis] = (face > 0.0f) ? 1.0f : -1.0f;
    out->objectId = (float)objectId;
    out->unlit = 1.0f;
}

static void AppendEdgeQuad(SceneVertex *vertices, int *cursor, int objectId,
                           int faceAxis, int uAxis, int vAxis, float face,
                           float u0, float v0, float u1, float v1)
{
    AppendEdgeVertex(vertices, cursor, objectId, faceAxis, uAxis, vAxis, face, u0, v0);
    AppendEdgeVertex(vertices, cursor, objectId, faceAxis, uAxis, vAxis, face, u1, v0);
    AppendEdgeVertex(vertices, cursor, objectId, faceAxis, uAxis, vAxis, face, u1, v1);
    AppendEdgeVertex(vertices, cursor, objectId, faceAxis, uAxis, vAxis, face, u0, v0);
    AppendEdgeVertex(vertices, cursor, objectId, faceAxis, uAxis, vAxis, face, u1, v1);
    AppendEdgeVertex(vertices, cursor, objectId, faceAxis, uAxis, vAxis, face, u0, v1);
}

static void AppendCubeEdgeRibbons(SceneVertex *vertices, int *cursor, int objectId)
{
    const float outer = 0.5f;
    const float inner = 0.455f;
    for (int faceAxis = 0; faceAxis < 3; faceAxis++) {
        int uAxis = (faceAxis + 1)%3;
        int vAxis = (faceAxis + 2)%3;
        for (int side = -1; side <= 1; side += 2) {
            float face = side*0.503f;
            AppendEdgeQuad(vertices, cursor, objectId, faceAxis, uAxis, vAxis,
                           face, -outer, -outer, outer, -inner);
            AppendEdgeQuad(vertices, cursor, objectId, faceAxis, uAxis, vAxis,
                           face, -outer, inner, outer, outer);
            AppendEdgeQuad(vertices, cursor, objectId, faceAxis, uAxis, vAxis,
                           face, -outer, -inner, -inner, inner);
            AppendEdgeQuad(vertices, cursor, objectId, faceAxis, uAxis, vAxis,
                           face, inner, -inner, outer, inner);
        }
    }
}

static GpuScene CreateGpuScene(Model sphere)
{
    GpuScene scene = { 0 };
    if (sphere.meshCount < 1) {
        fprintf(stderr, "GPU_SCENE missing required model geometry\n");
        return scene;
    }
    const int cubeVertices = 36;
    int sphereVertices = MeshTriangleVertexCount(&sphere.meshes[0]);
    scene.vertexCount = cubeVertices*7 + sphereVertices +
                        EDGE_VERTICES_PER_CUBE*ORBIT_COUNT;
    SceneVertex *vertices = calloc((size_t)scene.vertexCount, sizeof(*vertices));
    if (vertices == NULL) return scene;

    int cursor = 0;
    for (int object = 1; object <= 7; object++) {
        AppendCanonicalCube(vertices, &cursor, object);
        if (object <= ORBIT_COUNT) AppendCubeEdgeRibbons(vertices, &cursor, object);
    }
    AppendMeshVertices(vertices, &cursor, &sphere.meshes[0], 8, (Vector3){ 0 });

    static const char *vertexSource =
        "#version 100\n"
        "precision highp float;\n"
        "attribute vec3 aPosition; attribute vec3 aNormal; attribute float aObject; attribute float aUnlit;\n"
        "uniform mat4 uView; uniform mat4 uProjection;\n"
        "uniform vec4 uTransform[9]; uniform float uRotation[9]; uniform vec4 uColor[9];\n"
        "varying lowp vec4 vColor;\n"
        "void main(){ int id=int(aObject+0.5); vec4 tr; float angle; vec4 color;\n"
        "if(id==0){tr=uTransform[0];angle=uRotation[0];color=uColor[0];}\n"
        "else if(id==1){tr=uTransform[1];angle=uRotation[1];color=uColor[1];}\n"
        "else if(id==2){tr=uTransform[2];angle=uRotation[2];color=uColor[2];}\n"
        "else if(id==3){tr=uTransform[3];angle=uRotation[3];color=uColor[3];}\n"
        "else if(id==4){tr=uTransform[4];angle=uRotation[4];color=uColor[4];}\n"
        "else if(id==5){tr=uTransform[5];angle=uRotation[5];color=uColor[5];}\n"
        "else if(id==6){tr=uTransform[6];angle=uRotation[6];color=uColor[6];}\n"
        "else if(id==7){tr=uTransform[7];angle=uRotation[7];color=uColor[7];}\n"
        "else{tr=uTransform[8];angle=uRotation[8];color=uColor[8];}\n"
        "float c=cos(angle),s=sin(angle); vec3 p=aPosition*tr.w;\n"
        "p=vec3(c*p.x+s*p.z,p.y,-s*p.x+c*p.z)+tr.xyz;\n"
        "vec3 n=vec3(c*aNormal.x+s*aNormal.z,aNormal.y,-s*aNormal.x+c*aNormal.z);\n"
        "vec3 l=vec3(0.553,0.784,0.302); float d=max(dot(n,l),0.0);\n"
        "float sky=0.10*max(n.y,0.0); float bounce=0.16*max(-n.y,0.0);\n"
        "vec3 light=vec3(0.30,0.34,0.44)+vec3(1.0,0.91,0.82)*(d*0.68+sky)+vec3(0.22,0.34,0.48)*bounce;\n"
        "vColor=mix(vec4(color.rgb*light,color.a),vec4(1.0),aUnlit);\n"
        "gl_Position=uProjection*uView*vec4(p,1.0);}\n";
    static const char *fragmentSource =
        "#version 100\n"
        "precision lowp float; varying vec4 vColor;\n"
        "void main(){gl_FragColor=vColor;}\n";

    GLuint vs = CompileSceneShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = CompileSceneShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        free(vertices);
        return scene;
    }
    scene.program = glCreateProgram();
    glAttachShader(scene.program, vs);
    glAttachShader(scene.program, fs);
    glBindAttribLocation(scene.program, 0, "aPosition");
    glBindAttribLocation(scene.program, 1, "aNormal");
    glBindAttribLocation(scene.program, 2, "aObject");
    glBindAttribLocation(scene.program, 3, "aUnlit");
    glLinkProgram(scene.program);
    GLint linked = GL_FALSE;
    glGetProgramiv(scene.program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024] = { 0 };
        glGetProgramInfoLog(scene.program, sizeof(log), NULL, log);
        fprintf(stderr, "GPU_SCENE link failed: %s\n", log);
        glDeleteProgram(scene.program);
        scene.program = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!linked) {
        free(vertices);
        return scene;
    }

    scene.viewLocation = glGetUniformLocation(scene.program, "uView");
    scene.projectionLocation = glGetUniformLocation(scene.program, "uProjection");
    scene.transformLocation = glGetUniformLocation(scene.program, "uTransform");
    scene.rotationLocation = glGetUniformLocation(scene.program, "uRotation");
    scene.colorLocation = glGetUniformLocation(scene.program, "uColor");
    glGenBuffers(1, &scene.vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, scene.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(scene.vertexCount*sizeof(*vertices)),
                 vertices, GL_STATIC_DRAW);
    GLenum bufferError = glGetError();
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    free(vertices);
    printf("GPU_SCENE objects=%d vertices=%d baked_outlines=6 draws=1 linked=%d\n",
           GPU_OBJECT_COUNT, scene.vertexCount, linked);
    if (bufferError != GL_NO_ERROR) {
        fprintf(stderr, "GPU_SCENE vertex upload failed: 0x%x\n", bufferError);
        if (scene.vertexBuffer) glDeleteBuffers(1, &scene.vertexBuffer);
        if (scene.program) glDeleteProgram(scene.program);
        scene = (GpuScene){ 0 };
    }
    return scene;
}

static void DestroyGpuScene(GpuScene *scene)
{
    if (scene->vertexBuffer) glDeleteBuffers(1, &scene->vertexBuffer);
    if (scene->program) glDeleteProgram(scene->program);
}

static void DrawBackdrop(float time)
{
    // Clear is substantially cheaper on the GC880 than drawing a second
    // full-screen quad. Keep gradients as accents instead of paying for
    // multiple layers over all 2,073,600 pixels.
    ClearBackground((Color){ 8, 12, 29, 255 });
    DrawRectangleGradientV(0, 0, DESIGN_WIDTH, 150,
                           (Color){ 18, 30, 61, 255 },
                           (Color){ 8, 12, 29, 255 });
    DrawRectangleGradientV(0, DESIGN_HEIGHT - 180, DESIGN_WIDTH, 180,
                           (Color){ 8, 12, 29, 255 },
                           (Color){ 31, 9, 42, 255 });

    for (int i = 0; i < 2; i++) {
        float x = 390.0f + i*1120.0f + sinf(time*0.23f + i)*70.0f;
        float y = 180.0f + sinf(time*0.31f + i*1.7f)*90.0f;
        Color glow = (i & 1) ? (Color){ 20, 88, 122, 255 }
                             : (Color){ 104, 28, 87, 255 };
        DrawCircleV((Vector2){ x, y }, 62.0f, glow);
    }

    for (int band = 0; band < 5; band++) {
        int y = 700 + band*44 + (int)(sinf(time*0.6f + band)*18.0f);
        DrawRectangleGradientH(100, y, DESIGN_WIDTH - 200, 2,
                               (Color){ 20, 110, 135, 255 },
                               (Color){ 130, 38, 105, 255 });
    }
}

static void DrawParticles(const Particle *particles, float time)
{
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        float x = particles[i].base.x + sinf(time*particles[i].speed + particles[i].phase)*34.0f;
        float y = fmodf(particles[i].base.y - time*(18.0f + particles[i].speed*20.0f)
                        + DESIGN_HEIGHT*2.0f, DESIGN_HEIGHT + 120.0f) - 60.0f;
        float pulse = 0.65f + 0.35f*sinf(time*1.7f + particles[i].phase);
        Color color = (i%3 == 0) ? (Color){ 80, 225, 255, 255 }
                    : (i%3 == 1) ? (Color){ 255, 105, 205, 255 }
                                 : (Color){ 255, 220, 110, 255 };
        DrawCircleV((Vector2){ x, y }, particles[i].radius*pulse, color);
    }
}

static Camera3D DemoCamera(float time)
{
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 1.2f*sinf(time*0.10f), 3.3f, 9.5f };
    camera.target = (Vector3){ 0.0f, 1.2f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 48.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

static void DrawScene3D(float time, const GpuScene *scene,
                        Matrix *sharedView, Matrix *sharedProjection)
{
    Camera3D camera = DemoCamera(time);
    BeginMode3D(camera);
    // The DRM path should do this in BeginMode3D(), but keeping the depth pass
    // explicit avoids driver/state leakage from the preceding translucent 2D batch.
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    DrawGrid(8, 1.25f);
    rlDrawRenderBatchActive();

    GLfloat transforms[GPU_OBJECT_COUNT*4] = { 0 };
    GLfloat rotations[GPU_OBJECT_COUNT] = { 0 };
    for (int i = 0; i < ORBIT_COUNT; i++) {
        float a = time*(0.35f + i*0.025f) + i*(2.0f*PI/ORBIT_COUNT);
        float orbit = 3.0f + (i & 1)*0.8f;
        int object = i + 1;
        transforms[object*4 + 0] = cosf(a)*orbit;
        transforms[object*4 + 1] = 0.8f + sinf(time*0.8f + i)*0.9f;
        transforms[object*4 + 2] = sinf(a)*orbit;
        transforms[object*4 + 3] = 0.35f + (i%3)*0.10f;
        rotations[object] = a;
    }

    // Restore the original fixed side sculpture without adding another draw.
    transforms[7*4 + 0] = -4.5f;
    transforms[7*4 + 1] = 0.7f;
    transforms[7*4 + 2] = -2.0f;
    transforms[7*4 + 3] = 1.1f;
    rotations[7] = -12.0f*DEG2RAD;
    transforms[8*4 + 0] = 4.2f;
    transforms[8*4 + 1] = 0.75f;
    transforms[8*4 + 2] = 2.0f;
    transforms[8*4 + 3] = 1.5f;

    const GLfloat colors[GPU_OBJECT_COUNT*4] = {
        1.0f, 0.43f, 0.75f, 1.0f,
        1.0f, 0.76f, 0.25f, 1.0f,
        0.16f, 0.80f, 1.0f, 1.0f,
        1.0f, 0.76f, 0.25f, 1.0f,
        0.16f, 0.80f, 1.0f, 1.0f,
        1.0f, 0.76f, 0.25f, 1.0f,
        0.16f, 0.80f, 1.0f, 1.0f,
        0.38f, 0.35f, 0.96f, 1.0f,
        0.24f, 0.90f, 0.65f, 1.0f
    };
    Matrix view = rlGetMatrixModelview();
    Matrix projection = rlGetMatrixProjection();
    if (sharedView) *sharedView = view;
    if (sharedProjection) *sharedProjection = projection;
    float16 viewValues = MatrixToFloatV(view);
    float16 projectionValues = MatrixToFloatV(projection);
    glUseProgram(scene->program);
    glUniformMatrix4fv(scene->viewLocation, 1, GL_FALSE, viewValues.v);
    glUniformMatrix4fv(scene->projectionLocation, 1, GL_FALSE, projectionValues.v);
    glUniform4fv(scene->transformLocation, GPU_OBJECT_COUNT, transforms);
    glUniform1fv(scene->rotationLocation, GPU_OBJECT_COUNT, rotations);
    glUniform4fv(scene->colorLocation, GPU_OBJECT_COUNT, colors);
    glBindBuffer(GL_ARRAY_BUFFER, scene->vertexBuffer);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                          (const void *)offsetof(SceneVertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                          (const void *)offsetof(SceneVertex, normal));
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                          (const void *)offsetof(SceneVertex, objectId));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                          (const void *)offsetof(SceneVertex, unlit));
    glDrawArrays(GL_TRIANGLES, 0, scene->vertexCount);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    EndMode3D();
}

#endif

static double ProcessCpuMilliseconds(void)
{
    struct timespec value = { 0 };
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0) return 0.0;
    return value.tv_sec*1000.0 + value.tv_nsec/1000000.0;
}

static void DrawInterface(int fps, float time,
                          float cpuAverageMs, float gpuAverageMs,
                          bool automaticDensity)
{
    const int barX = 38;
    const int barY = DESIGN_HEIGHT - 58;
    const int barWidth = 630;
    const int meterY = barY + 17;
    const int meterWidth = 62;
    const float meterScale = (float)meterWidth/33.333f;
    int cpuWidth = (int)fminf(cpuAverageMs*meterScale, (float)meterWidth);
    int gpuWidth = (int)fminf(gpuAverageMs*meterScale, (float)meterWidth);
    const int cpuMeterX = barX + 421;
    const int gpuMeterX = barX + 548;

    DrawRectangle(barX, barY, barWidth, 40, (Color){ 4, 8, 22, 245 });
    DrawRectangleLines(barX, barY, barWidth, 40, (Color){ 65, 105, 145, 255 });
    DrawText(TextFormat("UP %ds", (int)time), barX + 14, barY + 12, 16,
             (Color){ 180, 205, 235, 255 });
    DrawText(TextFormat("%d FPS", fps), barX + 91, barY + 12, 16,
             (Color){ 255, 210, 70, 255 });
    DrawText(TextFormat("%dx%d", GetScreenWidth(), GetScreenHeight()),
             barX + 166, barY + 12, 16, RAYWHITE);
    DrawText(automaticDensity ? "AUTO" : "FIXED", barX + 274, barY + 12, 16,
             (Color){ 180, 205, 235, 255 });

    DrawText("CPU", barX + 380, barY + 12, 16, (Color){ 40, 205, 255, 255 });
    DrawRectangle(cpuMeterX, meterY, meterWidth, 6, (Color){ 35, 42, 66, 255 });
    DrawRectangle(cpuMeterX, meterY, cpuWidth, 6, (Color){ 40, 205, 255, 255 });
    DrawText("GPU", barX + 507, barY + 12, 16, (Color){ 255, 85, 180, 255 });
    DrawRectangle(gpuMeterX, meterY, meterWidth, 6, (Color){ 35, 42, 66, 255 });
    DrawRectangle(gpuMeterX, meterY, gpuWidth, 6, (Color){ 255, 85, 180, 255 });
}

int main(void)
{
    if (chdir("/tmp") != 0) fprintf(stderr, "Could not switch screenshot workspace to /tmp\n");
    MicroFxScene scriptScene;
    MicroFxSceneInit(&scriptScene);
    const char *scriptOverride = getenv("MICROFX_SCRIPT");
    const char *scriptPath = (scriptOverride != NULL && scriptOverride[0] != '\0')
                           ? scriptOverride
                           : (access("/data/apps/current/main.js", R_OK) == 0)
                           ? "/data/apps/current/main.js"
                           : "/usr/share/microfx/main.js";
    MicroFxScript *script = MicroFxScriptCreate(&scriptScene, scriptPath);
    if (script == NULL) exit(EXIT_FAILURE);
    RuntimeConfig runtime = LoadRuntimeConfig(&scriptScene);
    // The DRM backend already waits for the page-flip completion event. Asking
    // EGL for VSync as well can serialize two independent synchronization
    // points on etnaviv and quantize the application to a lower divisor of 60.
    SetConfigFlags(FLAG_FULLSCREEN_MODE);
    InitWindow(runtime.outputWidth, runtime.outputHeight, MICROFX_PRODUCT_NAME " DEMO");
    SetTargetFPS(0);

    EGLBoolean immediateSwap = eglSwapInterval(eglGetCurrentDisplay(), 0);
    printf("CANVAS_EGL swap_interval=0 result=%d error=0x%x\n",
           immediateSwap, eglGetError());

    GLint depthBits = 0;
    GLint samples = 0;
    glGetIntegerv(GL_DEPTH_BITS, &depthBits);
    glGetIntegerv(GL_SAMPLES, &samples);
    printf("CANVAS_GL depth_bits=%d samples=%d refresh=%d renderer=%s version=%s\n",
           depthBits, samples, GetMonitorRefreshRate(GetCurrentMonitor()),
           glGetString(GL_RENDERER), glGetString(GL_VERSION));
    printf("MICROFX_DISPLAY output=%dx%d target_fps=%d density=%s%.2f minimum=%.2f\n",
           GetScreenWidth(), GetScreenHeight(), runtime.targetFps,
           runtime.automaticDensity ? "auto:" : "fixed:", runtime.pixelDensity,
           runtime.minimumPixelDensity);
    fflush(stdout);

    MicroFxSdfRenderer sdfRenderer;
    if (!MicroFxSdfRendererInit(&sdfRenderer)) {
        fprintf(stderr, "MICROFX_SDF fatal initialization failure\n");
        exit(EXIT_FAILURE);
    }
    MicroFxQuadRenderer quadRenderer;
    if (!MicroFxQuadRendererInit(&quadRenderer)) {
        fprintf(stderr, "MICROFX_QUAD fatal initialization failure\n");
        exit(EXIT_FAILURE);
    }
    MicroFxMeshRenderer meshRenderer;
    if (!MicroFxMeshRendererInit(&meshRenderer)) {
        fprintf(stderr, "MICROFX_MESH fatal initialization failure\n");
        exit(EXIT_FAILURE);
    }
    MicroFxTextRenderer textRenderer;
    if (!MicroFxTextRendererInit(&textRenderer, GetFontDefault())) {
        fprintf(stderr, "MICROFX_TEXT fatal initialization failure\n");
        exit(EXIT_FAILURE);
    }
    int renderedFrames = 0;
    float cpuAverageMs = 0.0f;
    float gpuAverageMs = 0.0f;
    int renderWidth = GetScreenWidth();
    int renderHeight = GetScreenHeight();
    bool restartAtLowerResolution = false;
    float requestedRestartDensity = runtime.pixelDensity;
    double previousScriptTime = GetTime();
    while (!WindowShouldClose()) {
        float time = (float)GetTime();
        double scriptDelta = fmin(fmax((double)time - previousScriptTime, 0.0), 0.25);
        previousScriptTime = time;
        if (!MicroFxScriptUpdate(script, time, scriptDelta)) {
            fprintf(stderr, "MICROFX_JS fail-fast update error\n");
            break;
        }
        double cpuStartMs = ProcessCpuMilliseconds();
        double stage0 = GetTime();
        double stage1 = stage0;
        double stage2 = stage0;
        double stage3 = stage0;

        BeginDrawing();
        rlPushMatrix();
        rlScalef((float)GetScreenWidth()/DESIGN_WIDTH,
                 (float)GetScreenHeight()/DESIGN_HEIGHT, 1.0f);
        ClearBackground((Color){ 8, 12, 29, 255 });
        stage1 = GetTime();
        stage2 = GetTime();
        rlPopMatrix();
        rlDrawRenderBatchActive();
        MicroFxQuadRendererDraw(&quadRenderer,&scriptScene,
                               DESIGN_WIDTH,DESIGN_HEIGHT,true);
        Matrix scriptView = { 0 };
        Matrix scriptProjection = { 0 };
        Camera3D scriptCamera = { 0 };
        scriptCamera.position = (Vector3){ scriptScene.camera.position[0],
                                           scriptScene.camera.position[1],
                                           scriptScene.camera.position[2] };
        scriptCamera.target = (Vector3){ scriptScene.camera.target[0],
                                         scriptScene.camera.target[1],
                                         scriptScene.camera.target[2] };
        scriptCamera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        scriptCamera.fovy = scriptScene.camera.fovY;
        scriptCamera.projection = CAMERA_PERSPECTIVE;
        // The preceding 2D background pass disables depth writes. GLES depth
        // clears obey that mask, so restore it before clearing the new frame.
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        scriptView = MatrixLookAt(scriptCamera.position, scriptCamera.target,
                                  scriptCamera.up);
        scriptProjection = MatrixPerspective(scriptCamera.fovy*DEG2RAD,
                                              (double)GetScreenWidth()/GetScreenHeight(),
                                              0.05, 100.0);
        float16 scriptViewValues = MatrixToFloatV(scriptView);
        float16 scriptProjectionValues = MatrixToFloatV(scriptProjection);
        if (!MicroFxMeshRendererDraw(&meshRenderer, &scriptScene,
                                    scriptViewValues.v, scriptProjectionValues.v)) {
            fprintf(stderr, "MICROFX_MESH fatal asset or renderer failure\n");
            break;
        }
        // Retained 2D and text are overlays and therefore follow every 3D pass.
        MicroFxQuadRendererDraw(&quadRenderer, &scriptScene,
                               DESIGN_WIDTH, DESIGN_HEIGHT, false);
        MicroFxSdfRendererDraw(&sdfRenderer, &scriptScene,
                              DESIGN_WIDTH, DESIGN_HEIGHT);
        MicroFxTextRendererDraw(&textRenderer, &scriptScene,
                               DESIGN_WIDTH, DESIGN_HEIGHT);
        stage3 = GetTime();

        if (scriptScene.runtime.debugBar) {
            rlPushMatrix();
            rlScalef((float)GetScreenWidth()/DESIGN_WIDTH,
                     (float)GetScreenHeight()/DESIGN_HEIGHT, 1.0f);
            DrawInterface(GetFPS(), time, cpuAverageMs, gpuAverageMs,
                          runtime.automaticDensity);
            rlPopMatrix();
        }
        double stage4 = GetTime();
        EndDrawing();
        double stage5 = GetTime();
        double cpuEndMs = ProcessCpuMilliseconds();
        float frameCpuMs = (float)fmax(cpuEndMs - cpuStartMs, 0.0);
        float frameWallMs = (float)fmax((stage5 - stage0)*1000.0, 0.0);
        float frameGpuMs = fmaxf(frameWallMs - frameCpuMs, 0.0f);
        const float averageWeight = (renderedFrames < 30) ? 0.18f : 0.04f;
        if (renderedFrames == 0) {
            cpuAverageMs = frameCpuMs;
            gpuAverageMs = frameGpuMs;
        } else {
            cpuAverageMs += (frameCpuMs - cpuAverageMs)*averageWeight;
            gpuAverageMs += (frameGpuMs - gpuAverageMs)*averageWeight;
        }

        if (FileExists("/tmp/canvas-capture.request")) {
            remove("/tmp/canvas-capture.request");
            remove("/tmp/canvas-screen.png");
            remove("/tmp/canvas-screen.tmp.png");
            TakeScreenshot("/tmp/canvas-screen.tmp.png");
            if (rename("/tmp/canvas-screen.tmp.png", "/tmp/canvas-screen.png") == 0) {
                printf("CANVAS_CAPTURE /tmp/canvas-screen.png\n");
                fflush(stdout);
            }
        }

        renderedFrames++;
        if (scriptScene.runtime.durationSeconds > 0.0f &&
            time >= scriptScene.runtime.durationSeconds) {
            printf("MICROFX_DURATION complete seconds=%.1f\n",
                   scriptScene.runtime.durationSeconds);
            fflush(stdout);
            break;
        }
        if (runtime.automaticDensity && (renderedFrames % 180) == 0) {
            const float frameBudgetMs = 1000.0f/runtime.targetFps;
            const float averageFrameMs = cpuAverageMs + gpuAverageMs;
            if (averageFrameMs > frameBudgetMs*1.08f &&
                runtime.pixelDensity > runtime.minimumPixelDensity + 0.01f) {
                int nextWidth = GetScreenWidth();
                int nextHeight = GetScreenHeight();
                if (FindNextLowerDrmMode(&runtime, GetScreenWidth(), GetScreenHeight(),
                                         &requestedRestartDensity,
                                         &nextWidth, &nextHeight)) {
                    restartAtLowerResolution = true;
                    printf("MICROFX_RESOLUTION restart density=%.3f mode=%dx%d target_fps=%d average_ms=%.2f\n",
                           requestedRestartDensity, nextWidth, nextHeight,
                           runtime.targetFps, averageFrameMs);
                    fflush(stdout);
                    break;
                }
                printf("MICROFX_RESOLUTION minimum mode reached at %dx%d average_ms=%.2f\n",
                       GetScreenWidth(), GetScreenHeight(), averageFrameMs);
                fflush(stdout);
            }
        }
        if ((renderedFrames % 120) == 0) {
            printf("CANVAS_METRIC demo=2d3d-fillopt output=%dx%d render=%dx%d density=%.2f fps=%d frame_ms=%.2f\n",
                   GetScreenWidth(), GetScreenHeight(), renderWidth, renderHeight,
                   runtime.pixelDensity, GetFPS(), GetFrameTime()*1000.0f);
            printf("CANVAS_STAGES backdrop=%.2f particles=%.2f scene3d=%.2f ui=%.2f end=%.2f total=%.2f\n",
                   (stage1-stage0)*1000.0, (stage2-stage1)*1000.0,
                   (stage3-stage2)*1000.0, (stage4-stage3)*1000.0,
                   (stage5-stage4)*1000.0, (stage5-stage0)*1000.0);
            fflush(stdout);
        }
    }

    MicroFxScriptDestroy(script);
    MicroFxSdfRendererDestroy(&sdfRenderer);
    MicroFxQuadRendererDestroy(&quadRenderer);
    MicroFxMeshRendererDestroy(&meshRenderer);
    MicroFxTextRendererDestroy(&textRenderer);
    CloseWindow();
    if (restartAtLowerResolution) {
        char densityValue[24];
        snprintf(densityValue, sizeof(densityValue), "%.3f", requestedRestartDensity);
        setenv("MICROFX_AUTO_DENSITY_VALUE", densityValue, 1);
        execl("/proc/self/exe", "canvas-demo", (char *)NULL);
        perror("MICROFX_DENSITY exec");
        return EXIT_FAILURE;
    }
    return 0;
}
