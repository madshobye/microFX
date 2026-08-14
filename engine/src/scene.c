#include "microfx/scene.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void MicroFxSceneInit(MicroFxScene *scene)
{
    memset(scene, 0, sizeof(*scene));
    scene->clearColor[0] = 8.0f/255.0f;
    scene->clearColor[1] = 12.0f/255.0f;
    scene->clearColor[2] = 29.0f/255.0f;
    scene->camera.position[0] = 0.0f;
    scene->camera.position[1] = 3.0f;
    scene->camera.position[2] = 9.0f;
    scene->camera.target[1] = 1.0f;
    scene->camera.fovY = 48.0f;
    scene->runtime.targetFps = 30;
    scene->runtime.pixelDensity = 1.0f;
    scene->runtime.minimumPixelDensity = 0.5f;
    scene->runtime.colorFormat = MICROFX_COLOR_RGB565;
    scene->runtime.antialiasing = MICROFX_ANTIALIAS_NONE;
    scene->runtime.depthBits = 16;
    scene->runtime.automaticDensity = true;
    scene->runtime.dithering = true;
    scene->runtime.debugBarUntilSeconds = 10.0f*60.0f;
    scene->runtime.profiling = false;
    scene->runtime.profileIntervalFrames = 120;
    scene->runtime.densitySampleFrames = 60;
    scene->runtime.densityStep = 0.1f;
    scene->runtime.densityDownThreshold = 1.08f;
    scene->runtime.densityUpThreshold = 0.72f;
    scene->runtime.densityUpSamples = 4;
}

static int Add(MicroFxScene *scene, MicroFxSdfElement element)
{
    if (scene->sdfCount >= MICROFX_MAX_SDF_ELEMENTS) return -1;
    int handle = scene->sdfCount++;
    scene->sdf[handle] = element;
    scene->sdf[handle].opacity = 1.0f;
    scene->sdf[handle].visible = true;
    scene->sdfDirty = true;
    return MICROFX_HANDLE_SDF | handle;
}

int MicroFxSceneAddCircle(MicroFxScene *scene, float x, float y, float radius,
                         uint32_t color)
{
    return Add(scene, (MicroFxSdfElement){
        .kind = MICROFX_SDF_CIRCLE, .x = x, .y = y,
        .width = radius*2.0f, .height = radius*2.0f,
        .radius = radius, .color = color
    });
}

int MicroFxSceneAddRoundedRect(MicroFxScene *scene, float x, float y,
                              float width, float height, float radius,
                              uint32_t color)
{
    return Add(scene, (MicroFxSdfElement){
        .kind = radius > 0.0f ? MICROFX_SDF_ROUNDED_RECT : MICROFX_SDF_RECT,
        .x = x, .y = y,
        .width = width, .height = height, .radius = radius, .color = color
    });
}

static int AddQuad(MicroFxScene *scene, float x, float y, float width,
                   float height, uint32_t topColor, uint32_t bottomColor)
{
    if (scene->quadCount >= MICROFX_MAX_QUAD_ELEMENTS) return -1;
    int index = scene->quadCount++;
    scene->quad[index] = (MicroFxQuadElement){
        .kind = MICROFX_QUAD_RECT, .x = x, .y = y, .width = width, .height = height,
        .topColor = topColor, .bottomColor = bottomColor, .visible = true
    };
    scene->quad[index].opacity = 1.0f;
    scene->quadDirty = true;
    return MICROFX_HANDLE_QUAD | index;
}

int MicroFxSceneAddFastCircle(MicroFxScene *scene, float x, float y,
                             float radius, uint32_t color)
{
    int handle = AddQuad(scene, x, y, radius*2.0f, radius*2.0f, color, color);
    if (handle >= 0) scene->quad[handle & MICROFX_HANDLE_INDEX_MASK].kind = MICROFX_QUAD_CIRCLE;
    return handle;
}

int MicroFxSceneAddRect(MicroFxScene *scene, float x, float y,
                       float width, float height, uint32_t color)
{
    return AddQuad(scene, x, y, width, height, color, color);
}

int MicroFxSceneAddGradientRect(MicroFxScene *scene, float x, float y,
                               float width, float height, uint32_t topColor,
                               uint32_t bottomColor)
{
    return AddQuad(scene, x, y, width, height, topColor, bottomColor);
}

int MicroFxSceneAddBackground(MicroFxScene *scene, uint32_t topColor,
                             uint32_t bottomColor)
{
    int handle=MicroFxSceneAddGradientRect(scene,960,540,1920,1080,
                                          topColor,bottomColor);
    if(handle<0)return handle;
    scene->quad[handle&MICROFX_HANDLE_INDEX_MASK].background=true;
    scene->quadDirty=true;
    return handle;
}

bool MicroFxSceneHasOpaqueCoveringBackground(const MicroFxScene *scene)
{
    if (!scene) return false;
    for (int i = 0; i < scene->quadCount; i++) {
        const MicroFxQuadElement *element = &scene->quad[i];
        if (!element->background || !element->visible ||
            element->kind != MICROFX_QUAD_RECT ||
            fabsf(element->rotation) > 0.0001f || element->opacity < 0.999f ||
            (element->topColor & 255u) != 255u ||
            (element->bottomColor & 255u) != 255u) {
            continue;
        }
        const float left = element->x - element->width*0.5f;
        const float right = element->x + element->width*0.5f;
        const float top = element->y - element->height*0.5f;
        const float bottom = element->y + element->height*0.5f;
        if (left <= 0.0f && top <= 0.0f &&
            right >= MICROFX_DESIGN_WIDTH && bottom >= MICROFX_DESIGN_HEIGHT) {
            return true;
        }
    }
    return false;
}

bool MicroFxSceneMove(MicroFxScene *scene, int handle, float x, float y,
                     float rotation)
{
    int kind = handle & MICROFX_HANDLE_KIND_MASK;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    if (kind == MICROFX_HANDLE_SDF && index >= 0 && index < scene->sdfCount) {
        MicroFxSdfElement *element = &scene->sdf[index];
        element->x = x; element->y = y; element->rotation = rotation;
        scene->sdfDirty = true;
        return true;
    }
    if (kind == MICROFX_HANDLE_QUAD && index >= 0 && index < scene->quadCount) {
        MicroFxQuadElement *element = &scene->quad[index];
        element->x = x; element->y = y; element->rotation = rotation;
        scene->quadDirty = true;
        return true;
    }
    if (kind == MICROFX_HANDLE_TEXT && index >= 0 && index < scene->textCount) {
        MicroFxTextElement *element = &scene->text[index];
        element->x = x; element->y = y; element->rotation = rotation;
        scene->textDirty = true;
        return true;
    }
    if (kind == MICROFX_HANDLE_IMAGE && index >= 0 && index < scene->imageCount) {
        MicroFxImageElement *element = &scene->image[index];
        element->x = x; element->y = y; element->rotation = rotation;
        return true;
    }
    return false;
}

static int AddMesh(MicroFxScene *scene, MicroFxMeshKind kind, float x, float y,
                   float z, float scale, uint32_t color)
{
    if (scene->meshCount >= MICROFX_MAX_MESH_ELEMENTS) return -1;
    int index = scene->meshCount++;
    scene->mesh[index] = (MicroFxMeshElement){
        .kind = kind, .position = { x, y, z }, .scale = scale,
        .color = color, .shaderIndex = -1, .visible = true
    };
    scene->meshGeometryDirty = true;
    scene->meshStateDirty = true;
    return MICROFX_HANDLE_MESH | index;
}

int MicroFxSceneAddCube(MicroFxScene *scene, float x, float y, float z,
                       float scale, uint32_t color)
{
    return AddMesh(scene, MICROFX_MESH_CUBE, x, y, z, scale, color);
}

int MicroFxSceneAddSphere(MicroFxScene *scene, float x, float y, float z,
                         float scale, uint32_t color)
{
    return AddMesh(scene, MICROFX_MESH_SPHERE, x, y, z, scale, color);
}

int MicroFxSceneAddWireCube(MicroFxScene *scene, float x, float y, float z,
                           float scale, uint32_t color)
{
    return AddMesh(scene, MICROFX_MESH_WIRE_CUBE, x, y, z, scale, color);
}

int MicroFxSceneAddGrid(MicroFxScene *scene, float x, float y, float z,
                       float scale, uint32_t color)
{
    return AddMesh(scene, MICROFX_MESH_GRID, x, y, z, scale, color);
}

int MicroFxSceneAddModel(MicroFxScene *scene, const char *assetPath,
                        float x, float y, float z, float scale,
                        uint32_t color)
{
    if (!assetPath || !assetPath[0]) return -1;
    int handle = AddMesh(scene, MICROFX_MESH_MODEL, x, y, z, scale, color);
    if (handle < 0) return handle;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    snprintf(scene->mesh[index].assetPath,
             sizeof(scene->mesh[index].assetPath), "%s", assetPath);
    return handle;
}

bool MicroFxSceneTransform(MicroFxScene *scene, int handle, float x, float y,
                          float z, float rx, float ry, float rz, float scale)
{
    if ((handle & MICROFX_HANDLE_KIND_MASK) != MICROFX_HANDLE_MESH) return false;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    if (index < 0 || index >= scene->meshCount) return false;
    MicroFxMeshElement *element = &scene->mesh[index];
    element->position[0] = x; element->position[1] = y; element->position[2] = z;
    element->rotation[0] = rx; element->rotation[1] = ry; element->rotation[2] = rz;
    element->scale = scale;
    scene->meshStateDirty = true;
    return true;
}

int MicroFxSceneAddText(MicroFxScene *scene, const char *text, float x, float y,
                       float size, uint32_t color)
{
    if (scene->textCount >= MICROFX_MAX_TEXT_ELEMENTS) return -1;
    int index = scene->textCount++;
    MicroFxTextElement *element = &scene->text[index];
    snprintf(element->text, sizeof(element->text), "%s", text ? text : "");
    element->x = x; element->y = y; element->size = size;
    element->color = color; element->visible = true;
    element->opacity = 1.0f;
    scene->textDirty = true;
    return MICROFX_HANDLE_TEXT | index;
}

int MicroFxSceneAddImage(MicroFxScene *scene, const char *assetPath,
                        float x, float y, float scale, uint32_t tint)
{
    if (!assetPath || !assetPath[0] || scale <= 0.0f ||
        scene->imageCount >= MICROFX_MAX_IMAGE_ELEMENTS) return -1;
    int index = scene->imageCount++;
    MicroFxImageElement *element = &scene->image[index];
    snprintf(element->assetPath, sizeof(element->assetPath), "%s", assetPath);
    element->x = x; element->y = y; element->scale = scale;
    element->tint = tint; element->opacity = 1.0f; element->visible = true;
    scene->imageDirty = true;
    return MICROFX_HANDLE_IMAGE | index;
}

int MicroFxSceneAddBackgroundImage(MicroFxScene *scene, const char *assetPath,
                                   uint32_t tint)
{
    int handle=MicroFxSceneAddImage(scene,assetPath,
                                    MICROFX_DESIGN_WIDTH*0.5f,
                                    MICROFX_DESIGN_HEIGHT*0.5f,1.0f,tint);
    if(handle>=0)scene->image[handle&MICROFX_HANDLE_INDEX_MASK].background=true;
    return handle;
}

bool MicroFxSceneSetImageScale(MicroFxScene *scene, int handle, float scale)
{
    if ((handle & MICROFX_HANDLE_KIND_MASK) != MICROFX_HANDLE_IMAGE || scale <= 0.0f)
        return false;
    int index=handle & MICROFX_HANDLE_INDEX_MASK;
    if(index<0 || index>=scene->imageCount)return false;
    scene->image[index].scale=scale;scene->imageDirty=true;return true;
}

bool MicroFxSceneSetText(MicroFxScene *scene, int handle, const char *text)
{
    if ((handle & MICROFX_HANDLE_KIND_MASK) != MICROFX_HANDLE_TEXT) return false;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    if (index < 0 || index >= scene->textCount) return false;
    snprintf(scene->text[index].text, sizeof(scene->text[index].text), "%s",
             text ? text : "");
    scene->textDirty = true;
    return true;
}

bool MicroFxSceneSetTextFont(MicroFxScene *scene, int handle,
                             const char *assetPath)
{
    if ((handle & MICROFX_HANDLE_KIND_MASK) != MICROFX_HANDLE_TEXT || !assetPath)
        return false;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    if (index < 0 || index >= scene->textCount) return false;
    size_t length = strlen(assetPath);
    if (length >= sizeof(scene->text[index].fontPath)) return false;
    if (strcmp(scene->text[index].fontPath, assetPath) == 0) return true;
    memcpy(scene->text[index].fontPath, assetPath, length + 1);
    scene->textDirty = true;
    return true;
}

bool MicroFxSceneSetColor(MicroFxScene *scene, int handle, uint32_t color)
{
    int kind = handle & MICROFX_HANDLE_KIND_MASK;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    if (kind == MICROFX_HANDLE_SDF && index < scene->sdfCount) {
        scene->sdf[index].color = color; scene->sdfDirty = true; return true;
    }
    if (kind == MICROFX_HANDLE_MESH && index < scene->meshCount) {
        scene->mesh[index].color = color; scene->meshStateDirty = true; return true;
    }
    if (kind == MICROFX_HANDLE_TEXT && index < scene->textCount) {
        scene->text[index].color = color; scene->textDirty = true; return true;
    }
    if (kind == MICROFX_HANDLE_QUAD && index < scene->quadCount) {
        scene->quad[index].topColor = color;
        scene->quad[index].bottomColor = color;
        scene->quadDirty = true;
        return true;
    }
    if (kind == MICROFX_HANDLE_IMAGE && index < scene->imageCount) {
        scene->image[index].tint = color; return true;
    }
    return false;
}

bool MicroFxSceneSetOpacity(MicroFxScene *scene, int handle, float opacity)
{
    if (opacity < 0.0f || opacity > 1.0f) return false;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    switch (handle & MICROFX_HANDLE_KIND_MASK) {
    case MICROFX_HANDLE_SDF:
        if (index < 0 || index >= scene->sdfCount) return false;
        scene->sdf[index].opacity = opacity; scene->sdfDirty = true; return true;
    case MICROFX_HANDLE_QUAD:
        if (index < 0 || index >= scene->quadCount) return false;
        scene->quad[index].opacity = opacity; scene->quadDirty = true; return true;
    case MICROFX_HANDLE_TEXT:
        if (index < 0 || index >= scene->textCount) return false;
        scene->text[index].opacity = opacity; scene->textDirty = true; return true;
    case MICROFX_HANDLE_IMAGE:
        if (index < 0 || index >= scene->imageCount) return false;
        scene->image[index].opacity = opacity; return true;
    default:
        return false;
    }
}

bool MicroFxSceneSetVisible(MicroFxScene *scene, int handle, bool visible)
{
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    switch (handle & MICROFX_HANDLE_KIND_MASK) {
    case MICROFX_HANDLE_SDF:
        if (index < 0 || index >= scene->sdfCount) return false;
        if (scene->sdf[index].visible == visible) return true;
        scene->sdf[index].visible = visible; scene->sdfDirty = true; return true;
    case MICROFX_HANDLE_QUAD:
        if (index < 0 || index >= scene->quadCount) return false;
        if (scene->quad[index].visible == visible) return true;
        scene->quad[index].visible = visible; scene->quadDirty = true; return true;
    case MICROFX_HANDLE_MESH:
        if (index < 0 || index >= scene->meshCount) return false;
        if (scene->mesh[index].visible == visible) return true;
        scene->mesh[index].visible = visible;
        scene->meshGeometryDirty = true;
        scene->meshStateDirty = true;
        return true;
    case MICROFX_HANDLE_TEXT:
        if (index < 0 || index >= scene->textCount) return false;
        if (scene->text[index].visible == visible) return true;
        scene->text[index].visible = visible; scene->textDirty = true; return true;
    case MICROFX_HANDLE_IMAGE:
        if (index < 0 || index >= scene->imageCount) return false;
        if (scene->image[index].visible == visible) return true;
        scene->image[index].visible = visible; return true;
    default: return false;
    }
}

bool MicroFxSceneSetEffect(MicroFxScene *scene, int handle, int effect,
                          float amount, float scale)
{
    if ((handle & MICROFX_HANDLE_KIND_MASK) != MICROFX_HANDLE_MESH) return false;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    if (index < 0 || index >= scene->meshCount || effect < 0 || effect > 3) return false;
    scene->mesh[index].effect[0] = (float)effect;
    scene->mesh[index].effect[1] = amount;
    scene->mesh[index].effect[2] = scale;
    scene->meshStateDirty = true;
    return true;
}

bool MicroFxSceneSetMeshShader(MicroFxScene *scene, int handle,
                               const char *vertexPath,
                               const char *fragmentPath)
{
    if ((handle & MICROFX_HANDLE_KIND_MASK) != MICROFX_HANDLE_MESH ||
        !fragmentPath || !fragmentPath[0]) return false;
    int elementIndex=handle & MICROFX_HANDLE_INDEX_MASK;
    if(elementIndex<0||elementIndex>=scene->meshCount)return false;
    const char *vertex=vertexPath?vertexPath:"";
    int shaderIndex=-1;
    for(int i=0;i<scene->meshShaderCount;i++){
        if(strcmp(scene->meshShader[i].vertexPath,vertex)==0&&
           strcmp(scene->meshShader[i].fragmentPath,fragmentPath)==0){
            shaderIndex=i;break;
        }
    }
    if(shaderIndex<0){
        if(scene->meshShaderCount>=MICROFX_MAX_MESH_SHADERS)return false;
        shaderIndex=scene->meshShaderCount++;
        snprintf(scene->meshShader[shaderIndex].vertexPath,
                 sizeof(scene->meshShader[shaderIndex].vertexPath),"%s",vertex);
        snprintf(scene->meshShader[shaderIndex].fragmentPath,
                 sizeof(scene->meshShader[shaderIndex].fragmentPath),"%s",fragmentPath);
    }
    scene->mesh[elementIndex].shaderIndex=shaderIndex;
    scene->meshGeometryDirty=true;
    return true;
}

void MicroFxSceneSetCamera(MicroFxScene *scene, float x, float y, float z,
                          float targetX, float targetY, float targetZ,
                          float fovY)
{
    scene->camera.position[0] = x; scene->camera.position[1] = y;
    scene->camera.position[2] = z; scene->camera.target[0] = targetX;
    scene->camera.target[1] = targetY; scene->camera.target[2] = targetZ;
    scene->camera.fovY = fovY;
}
