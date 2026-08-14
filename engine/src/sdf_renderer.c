#include "microfx/sdf_renderer.h"
#include "microfx/gpu_math.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    float position[2];
    float local[2];
    float halfSize[2];
    float radius;
    float kind;
    float color[4];
} SdfVertex;

static GLuint Compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = { 0 };
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "MICROFX_SDF shader failure: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool MicroFxSdfRendererInit(MicroFxSdfRenderer *renderer)
{
    *renderer = (MicroFxSdfRenderer){ 0 };
    static const char *vs =
        "#version 100\nprecision highp float;\n"
        "attribute vec2 aPosition; attribute vec2 aLocal; attribute vec2 aHalfSize;\n"
        "attribute float aRadius; attribute float aKind; attribute vec4 aColor;\n"
        "uniform vec2 uViewport; varying vec2 vLocal; varying vec2 vHalfSize;\n"
        "varying float vRadius; varying float vKind; varying lowp vec4 vColor;\n"
        "void main(){vec2 p=vec2(aPosition.x/uViewport.x*2.0-1.0,1.0-aPosition.y/uViewport.y*2.0);"
        "gl_Position=vec4(p,0.0,1.0);vLocal=aLocal;vHalfSize=aHalfSize;"
        "vRadius=aRadius;vKind=aKind;vColor=aColor;}\n";
    static const char *fs =
        "#version 100\n#extension GL_OES_standard_derivatives : enable\nprecision mediump float;\n"
        "varying vec2 vLocal; varying vec2 vHalfSize; varying float vRadius;"
        "varying float vKind; varying lowp vec4 vColor;\n"
        MICROFX_GLSL_LENGTH2
        "void main(){if(vKind>1.5){gl_FragColor=vColor;return;}float d;"
        "if(vKind<0.5)d=microfxLength2(vLocal)-vHalfSize.x;"
        "else{vec2 q=abs(vLocal)-vHalfSize+vec2(vRadius);"
        "d=microfxLength2(max(q,0.0))+min(max(q.x,q.y),0.0)-vRadius;}"
        "float aa=max(fwidth(d),0.75);float alpha=1.0-smoothstep(-aa,aa,d);"
        "gl_FragColor=vec4(vColor.rgb,vColor.a*alpha);}\n";
    GLuint vertex = Compile(GL_VERTEX_SHADER, vs);
    GLuint fragment = Compile(GL_FRAGMENT_SHADER, fs);
    if (!vertex || !fragment) return false;
    renderer->program = glCreateProgram();
    glAttachShader(renderer->program, vertex);
    glAttachShader(renderer->program, fragment);
    glBindAttribLocation(renderer->program, 0, "aPosition");
    glBindAttribLocation(renderer->program, 1, "aLocal");
    glBindAttribLocation(renderer->program, 2, "aHalfSize");
    glBindAttribLocation(renderer->program, 3, "aRadius");
    glBindAttribLocation(renderer->program, 4, "aKind");
    glBindAttribLocation(renderer->program, 5, "aColor");
    glLinkProgram(renderer->program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(renderer->program, GL_LINK_STATUS, &linked);
    if (!linked) return false;
    renderer->viewportLocation = glGetUniformLocation(renderer->program, "uViewport");
    glGenBuffers(1, &renderer->vertexBuffer);
    return renderer->vertexBuffer != 0;
}

static void SetVertex(SdfVertex *vertex, const MicroFxSdfElement *e,
                      float localX, float localY)
{
    float c = cosf(e->rotation), s = sinf(e->rotation);
    vertex->position[0] = e->x + c*localX - s*localY;
    vertex->position[1] = e->y + s*localX + c*localY;
    vertex->local[0] = localX;
    vertex->local[1] = localY;
    vertex->halfSize[0] = e->width*0.5f;
    vertex->halfSize[1] = e->height*0.5f;
    vertex->radius = e->radius;
    vertex->kind = (float)e->kind;
    vertex->color[0] = ((e->color >> 24) & 255)/255.0f;
    vertex->color[1] = ((e->color >> 16) & 255)/255.0f;
    vertex->color[2] = ((e->color >> 8) & 255)/255.0f;
    vertex->color[3] = (e->color & 255)/255.0f*e->opacity;
}

void MicroFxSdfRendererDraw(MicroFxSdfRenderer *renderer, MicroFxScene *scene,
                           int width, int height)
{
    if (!renderer->program || scene->sdfCount == 0) return;
    if (scene->sdfDirty) {
        SdfVertex vertices[MICROFX_MAX_SDF_ELEMENTS*6];
        int cursor = 0;
        const int corners[6][2] = {{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
        for (int i = 0; i < scene->sdfCount; i++) {
            const MicroFxSdfElement *e = &scene->sdf[i];
            if (!e->visible) continue;
            for (int v = 0; v < 6; v++) {
                SetVertex(&vertices[cursor++], e, corners[v][0]*e->width*0.5f,
                          corners[v][1]*e->height*0.5f);
            }
        }
        renderer->vertexCount = cursor;
        glBindBuffer(GL_ARRAY_BUFFER, renderer->vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, cursor*(GLsizeiptr)sizeof(SdfVertex),
                     vertices, GL_DYNAMIC_DRAW);
        scene->sdfDirty = false;
    } else glBindBuffer(GL_ARRAY_BUFFER, renderer->vertexBuffer);

    glUseProgram(renderer->program);
    glUniform2f(renderer->viewportLocation, (float)width, (float)height);
    for (int i = 0; i < 6; i++) glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SdfVertex), (void *)offsetof(SdfVertex, position));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SdfVertex), (void *)offsetof(SdfVertex, local));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SdfVertex), (void *)offsetof(SdfVertex, halfSize));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(SdfVertex), (void *)offsetof(SdfVertex, radius));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(SdfVertex), (void *)offsetof(SdfVertex, kind));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(SdfVertex), (void *)offsetof(SdfVertex, color));
    // Raw GLES renderers must not inherit raster state from raylib's 3D pass.
    // The screen-space triangles are clockwise after the Y-axis conversion and
    // would otherwise be discarded when GL_CULL_FACE remains enabled.
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, renderer->vertexCount);
    for (int i = 0; i < 6; i++) glDisableVertexAttribArray((GLuint)i);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void MicroFxSdfRendererDestroy(MicroFxSdfRenderer *renderer)
{
    if (renderer->vertexBuffer) glDeleteBuffers(1, &renderer->vertexBuffer);
    if (renderer->program) glDeleteProgram(renderer->program);
    *renderer = (MicroFxSdfRenderer){ 0 };
}
