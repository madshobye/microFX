#include "microfx/scene.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
    scene->runtime.debugBarStyle = MICROFX_DEBUG_BAR_STANDARD;
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
    if (kind == MICROFX_HANDLE_OUTLINE && index >= 0 && index < scene->outlineCount) {
        MicroFxOutlineElement *element = &scene->outline[index];
        element->x = x; element->y = y; element->rotation = rotation;
        scene->outlineDirty = true;
        return true;
    }
    return false;
}

bool MicroFxSceneSetSdfGeometry(MicroFxScene *scene, int handle,
                                MicroFxSdfKind kind, float width,
                                float height, float radius)
{
    if ((handle & MICROFX_HANDLE_KIND_MASK) != MICROFX_HANDLE_SDF ||
        kind < MICROFX_SDF_CIRCLE || kind > MICROFX_SDF_RECT ||
        width <= 0.0f || height <= 0.0f || radius < 0.0f) return false;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    if (index < 0 || index >= scene->sdfCount) return false;
    MicroFxSdfElement *element = &scene->sdf[index];
    element->kind = kind; element->width = width; element->height = height;
    element->radius = radius; scene->sdfDirty = true;
    return true;
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
    element->opacity = 1.0f; element->antialias = true;
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

int MicroFxSceneAddOutline(MicroFxScene *scene, const float points[][2],
                           int pointCount, float x, float y, float scale,
                           float width, uint32_t color, bool closed)
{
    if(!points||pointCount<2||pointCount>MICROFX_MAX_OUTLINE_POINTS||
       scale<=0.0f||width<=0.0f||scene->outlineCount>=MICROFX_MAX_OUTLINE_ELEMENTS)
        return -1;
    int index=scene->outlineCount++;
    MicroFxOutlineElement *element=&scene->outline[index];
    memcpy(element->points,points,(size_t)pointCount*sizeof(element->points[0]));
    element->pointCount=pointCount;element->x=x;element->y=y;element->scale=scale;
    element->width=width;element->color=color;element->opacity=1.0f;
    element->visible=true;element->closed=closed;scene->outlineDirty=true;
    return MICROFX_HANDLE_OUTLINE|index;
}

int MicroFxSceneAddPolygon(MicroFxScene *scene, const float points[][2],
                           int pointCount, float x, float y, float scale,
                           uint32_t color)
{
    if(!points||pointCount<3||pointCount>MICROFX_MAX_OUTLINE_POINTS||
       scale<=0.0f||scene->outlineCount>=MICROFX_MAX_OUTLINE_ELEMENTS)
        return -1;
    int index=scene->outlineCount++;
    MicroFxOutlineElement *element=&scene->outline[index];
    memcpy(element->points,points,(size_t)pointCount*sizeof(element->points[0]));
    element->pointCount=pointCount;element->x=x;element->y=y;element->scale=scale;
    element->width=0.0f;element->color=color;element->opacity=1.0f;
    element->visible=true;element->closed=true;element->filled=true;
    scene->outlineDirty=true;
    return MICROFX_HANDLE_OUTLINE|index;
}

bool MicroFxSceneSetOutlinePoints(MicroFxScene *scene, int handle,
                                  const float points[][2], int pointCount)
{
    if((handle&MICROFX_HANDLE_KIND_MASK)!=MICROFX_HANDLE_OUTLINE||!points||
       pointCount<2||pointCount>MICROFX_MAX_OUTLINE_POINTS)return false;
    int index=handle&MICROFX_HANDLE_INDEX_MASK;
    if(index<0||index>=scene->outlineCount)return false;
    MicroFxOutlineElement *element=&scene->outline[index];
    memcpy(element->points,points,(size_t)pointCount*sizeof(element->points[0]));
    element->pointCount=pointCount;scene->outlineDirty=true;return true;
}

bool MicroFxSceneSetOutlineScale(MicroFxScene *scene, int handle, float scale)
{
    if((handle&MICROFX_HANDLE_KIND_MASK)!=MICROFX_HANDLE_OUTLINE||scale<=0.0f)
        return false;
    int index=handle&MICROFX_HANDLE_INDEX_MASK;
    if(index<0||index>=scene->outlineCount)return false;
    if(scene->outline[index].scale==scale)return true;
    scene->outline[index].scale=scale;scene->outlineDirty=true;return true;
}

int MicroFxSceneAddTileMap(MicroFxScene *scene, float grayscale,
                           float contrast, float brightness, float invert,
                           uint32_t tint)
{
    if(scene->tileMapCount>=MICROFX_MAX_TILE_MAPS||grayscale<0.0f||
       grayscale>1.0f||contrast<0.0f||brightness<0.0f||invert<0.0f||invert>1.0f)
        return -1;
    int handle=scene->tileMapCount++;
    scene->tileMap[handle]=(MicroFxTileMap){.configured=true,.visible=true,
        .grayscale=grayscale,.contrast=contrast,.brightness=brightness,
        .invert=invert,.tint=tint};
    return handle;
}

bool MicroFxSceneBeginTileMap(MicroFxScene *scene,int handle,int generation,
                              int tileCount)
{
    if(handle<0||handle>=scene->tileMapCount||generation<=0||tileCount<=0||
       tileCount>MICROFX_MAX_TILE_MAP_TILES)return false;
    MicroFxTileMap *map=&scene->tileMap[handle];
    for(int i=0;i<MICROFX_MAX_TILE_MAP_TILES;i++){
        free(map->tiles[i].encoded);
        map->tiles[i]=(MicroFxTileMapTile){0};
    }
    map->generation=generation;map->tileCount=tileCount;
    return true;
}

bool MicroFxSceneSubmitTileMapTile(MicroFxScene *scene,int handle,
                                   int generation,int index,float x,float y,
                                   float size,const uint8_t *encoded,
                                   size_t encodedSize)
{
    if(handle<0||handle>=scene->tileMapCount||index<0||
       index>=scene->tileMap[handle].tileCount||size<=0.0f||!encoded||
       encodedSize==0||encodedSize>256*1024)return false;
    MicroFxTileMap *map=&scene->tileMap[handle];
    if(generation!=map->generation)return false;
    uint8_t *copy=malloc(encodedSize);if(!copy)return false;
    memcpy(copy,encoded,encodedSize);
    MicroFxTileMapTile *tile=&map->tiles[index];
    free(tile->encoded);*tile=(MicroFxTileMapTile){.x=x,.y=y,.size=size,
        .encoded=copy,.encodedSize=encodedSize,.received=true};
    return true;
}

bool MicroFxSceneSetTileMapVisible(MicroFxScene *scene,int handle,bool visible)
{
    if(handle<0||handle>=scene->tileMapCount)return false;
    scene->tileMap[handle].visible=visible;return true;
}

static MicroFxGpuTexture *GpuTexture(MicroFxScene *scene,int handle)
{
    if((handle&MICROFX_HANDLE_KIND_MASK)!=MICROFX_HANDLE_GPU_TEXTURE)return NULL;
    int index=handle&MICROFX_HANDLE_INDEX_MASK;
    if(index<0||index>=scene->gpuTextureCount)return NULL;
    return &scene->gpuTexture[index];
}

int MicroFxSceneAddGpuMapTexture(MicroFxScene *scene,int mapIndex)
{
    if(scene->gpuTextureCount>=MICROFX_MAX_GPU_TEXTURES||mapIndex<0||
       mapIndex>=scene->tileMapCount)return -1;
    int index=scene->gpuTextureCount++;
    scene->gpuTexture[index]=(MicroFxGpuTexture){.source=MICROFX_GPU_TEXTURE_MAP,
        .stage=MICROFX_GPU_TEXTURE_OVERLAY,.mapIndex=mapIndex,
        .secondaryMapIndex=-1,.tertiaryMapIndex=-1,.visible=true,
        .blend=true,.opacity=1.0f};
    return MICROFX_HANDLE_GPU_TEXTURE|index;
}

int MicroFxSceneAddGpuAssetTexture(MicroFxScene *scene,const char *assetPath)
{
    if(scene->gpuTextureCount>=MICROFX_MAX_GPU_TEXTURES||!assetPath||
       !assetPath[0]||strlen(assetPath)>=MICROFX_MAX_ASSET_PATH)return -1;
    int index=scene->gpuTextureCount++;
    MicroFxGpuTexture *texture=&scene->gpuTexture[index];
    *texture=(MicroFxGpuTexture){.source=MICROFX_GPU_TEXTURE_ASSET,
        .stage=MICROFX_GPU_TEXTURE_BACKGROUND,.mapIndex=-1,
        .secondaryMapIndex=-1,.tertiaryMapIndex=-1,.visible=true,.opacity=1.0f};
    snprintf(texture->assetPath,sizeof(texture->assetPath),"%s",assetPath);
    return MICROFX_HANDLE_GPU_TEXTURE|index;
}

int MicroFxSceneAddGpuRasterTexture(MicroFxScene *scene,int width,int height)
{
    if(scene->gpuTextureCount>=MICROFX_MAX_GPU_TEXTURES||width<1||height<1||
       width>MICROFX_DESIGN_WIDTH||height>MICROFX_DESIGN_HEIGHT)return -1;
    const size_t size=(size_t)width*(size_t)height*4u;
    uint8_t *pixels=calloc(size,1);if(!pixels)return -1;
    int index=scene->gpuTextureCount++;
    scene->gpuTexture[index]=(MicroFxGpuTexture){
        .source=MICROFX_GPU_TEXTURE_RASTER,
        .stage=MICROFX_GPU_TEXTURE_BACKGROUND,.mapIndex=-1,
        .secondaryMapIndex=-1,.tertiaryMapIndex=-1,.visible=true,
        .blend=true,.opacity=1.0f,.rasterRgba=pixels,.rasterSize=size,
        .rasterWidth=width,.rasterHeight=height,.rasterVersion=1};
    return MICROFX_HANDLE_GPU_TEXTURE|index;
}

bool MicroFxSceneClearGpuRasterTexture(MicroFxScene *scene,int handle,
                                       uint32_t color)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||texture->source!=MICROFX_GPU_TEXTURE_RASTER||
       !texture->rasterRgba)return false;
    const uint8_t rgba[4]={(uint8_t)(color>>24),(uint8_t)(color>>16),
                           (uint8_t)(color>>8),(uint8_t)color};
    if(color==0){memset(texture->rasterRgba,0,texture->rasterSize);return true;}
    for(size_t offset=0;offset<texture->rasterSize;offset+=4)
        memcpy(texture->rasterRgba+offset,rgba,4);
    return true;
}

static void BlendRasterPixel(MicroFxGpuTexture *texture,int x,int y,
                             uint32_t color,float coverage)
{
    if(x<0||x>=texture->rasterWidth||y<0||y>=texture->rasterHeight||coverage<=0)
        return;
    uint8_t *pixel=texture->rasterRgba+
        ((size_t)y*(size_t)texture->rasterWidth+(size_t)x)*4u;
    const int sourceAlpha=(int)roundf((float)(color&255u)*fminf(coverage,1.0f));
    if(sourceAlpha<=0)return;
    const int inverse=255-sourceAlpha;
    const int destinationAlpha=pixel[3];
    const int outputAlpha=sourceAlpha+(destinationAlpha*inverse+127)/255;
    const int source[3]={(int)((color>>24)&255u),(int)((color>>16)&255u),
                         (int)((color>>8)&255u)};
    for(int channel=0;channel<3;channel++){
        const int premultiplied=source[channel]*sourceAlpha+
            (pixel[channel]*destinationAlpha*inverse+127)/255;
        pixel[channel]=(uint8_t)(outputAlpha>0?
            (premultiplied+outputAlpha/2)/outputAlpha:0);
    }
    pixel[3]=(uint8_t)outputAlpha;
}

bool MicroFxSceneDrawGpuRasterPath(MicroFxScene *scene,int handle,
                                   const float points[][2],int pointCount,
                                   float width,uint32_t color)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||texture->source!=MICROFX_GPU_TEXTURE_RASTER||!points||
       pointCount<2||pointCount>MICROFX_MAX_OUTLINE_POINTS||
       !isfinite(width)||width<=0.0f||width>64.0f)return false;
    const float radius=fmaxf(0.5f,width*0.5f);
    const int stamp=(int)ceilf(radius+0.5f);
    for(int index=1;index<pointCount;index++){
        const float x0=points[index-1][0],y0=points[index-1][1];
        const float dx=points[index][0]-x0,dy=points[index][1]-y0;
        if(!isfinite(x0)||!isfinite(y0)||!isfinite(dx)||!isfinite(dy))return false;
        const int steps=(int)ceilf(fmaxf(fabsf(dx),fabsf(dy))*2.0f);
        for(int step=0;step<=steps;step++){
            const float amount=steps>0?(float)step/(float)steps:0.0f;
            const float cx=x0+dx*amount,cy=y0+dy*amount;
            const int centerX=(int)floorf(cx),centerY=(int)floorf(cy);
            for(int oy=-stamp;oy<=stamp;oy++)for(int ox=-stamp;ox<=stamp;ox++){
                const float px=(float)(centerX+ox)+0.5f;
                const float py=(float)(centerY+oy)+0.5f;
                const float distance=sqrtf((px-cx)*(px-cx)+(py-cy)*(py-cy));
                BlendRasterPixel(texture,centerX+ox,centerY+oy,color,
                                 radius+0.5f-distance);
            }
        }
    }
    return true;
}

bool MicroFxSceneCommitGpuRasterTexture(MicroFxScene *scene,int handle)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||texture->source!=MICROFX_GPU_TEXTURE_RASTER||
       !texture->rasterRgba)return false;
    texture->rasterVersion++;return true;
}

bool MicroFxSceneSetGpuTextureOverlayRaster(MicroFxScene *scene,int handle,
                                            int rasterHandle)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    MicroFxGpuTexture *raster=GpuTexture(scene,rasterHandle);
    if(!texture||!raster||texture==raster||
       raster->source!=MICROFX_GPU_TEXTURE_RASTER)return false;
    texture->overlayRaster=true;
    texture->overlayTextureIndex=rasterHandle&MICROFX_HANDLE_INDEX_MASK;
    texture->shaderVersion++;return true;
}

bool MicroFxSceneSetGpuTextureSecondaryMap(MicroFxScene *scene,int handle,
                                           int mapIndex)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||mapIndex<0||mapIndex>=scene->tileMapCount)return false;
    texture->secondary=true;texture->secondarySource=MICROFX_GPU_TEXTURE_MAP;
    texture->secondaryMapIndex=mapIndex;texture->secondaryAssetPath[0]='\0';
    texture->secondaryVersion++;texture->shaderVersion++;return true;
}

bool MicroFxSceneSetGpuTextureSecondaryAsset(MicroFxScene *scene,int handle,
                                             const char *assetPath)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||!assetPath||!assetPath[0]||
       strlen(assetPath)>=MICROFX_MAX_ASSET_PATH)return false;
    texture->secondary=true;texture->secondarySource=MICROFX_GPU_TEXTURE_ASSET;
    texture->secondaryMapIndex=-1;
    snprintf(texture->secondaryAssetPath,sizeof(texture->secondaryAssetPath),
             "%s",assetPath);
    texture->secondaryVersion++;texture->shaderVersion++;return true;
}

bool MicroFxSceneSetGpuTextureTertiaryMap(MicroFxScene *scene,int handle,
                                          int mapIndex)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||mapIndex<0||mapIndex>=scene->tileMapCount)return false;
    texture->tertiary=true;texture->tertiarySource=MICROFX_GPU_TEXTURE_MAP;
    texture->tertiaryMapIndex=mapIndex;texture->tertiaryAssetPath[0]='\0';
    texture->tertiaryVersion++;texture->shaderVersion++;return true;
}

bool MicroFxSceneSetGpuTextureTertiaryAsset(MicroFxScene *scene,int handle,
                                            const char *assetPath)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||!assetPath||!assetPath[0]||
       strlen(assetPath)>=MICROFX_MAX_ASSET_PATH)return false;
    texture->tertiary=true;texture->tertiarySource=MICROFX_GPU_TEXTURE_ASSET;
    texture->tertiaryMapIndex=-1;
    snprintf(texture->tertiaryAssetPath,sizeof(texture->tertiaryAssetPath),
             "%s",assetPath);
    texture->tertiaryVersion++;texture->shaderVersion++;return true;
}

bool MicroFxSceneSetGpuTextureShader(MicroFxScene *scene,int handle,
                                     const char *fragmentPath)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||!fragmentPath||!fragmentPath[0]||
       strlen(fragmentPath)>=MICROFX_MAX_ASSET_PATH)return false;
    if(strcmp(texture->fragmentPath,fragmentPath)==0)return true;
    snprintf(texture->fragmentPath,sizeof(texture->fragmentPath),"%s",fragmentPath);
    texture->shaderVersion++;return true;
}

bool MicroFxSceneSetGpuTextureParams(MicroFxScene *scene,int handle,
                                     const float *params,int count)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||count<0||count>MICROFX_MAX_GPU_TEXTURE_PARAMS||
       (count>0&&!params))return false;
    if(texture->paramCount==count&&
       (count==0||memcmp(texture->params,params,(size_t)count*sizeof(float))==0))
        return true;
    memset(texture->params,0,sizeof(texture->params));
    if(count>0)memcpy(texture->params,params,(size_t)count*sizeof(float));
    texture->paramCount=count;texture->paramVersion++;return true;
}

bool MicroFxSceneSetGpuTextureField(MicroFxScene *scene,int handle,int width,
                                    int height,const uint8_t *rgba,size_t size)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||width<=0||height<=0||
       width>MICROFX_MAX_GPU_TEXTURE_FIELD_SIZE||
       height>MICROFX_MAX_GPU_TEXTURE_FIELD_SIZE||!rgba||
       size!=(size_t)width*(size_t)height*4u)return false;
    uint8_t *copy=malloc(size);if(!copy)return false;
    memcpy(copy,rgba,size);free(texture->fieldRgba);
    texture->fieldRgba=copy;texture->fieldSize=size;
    texture->fieldWidth=width;texture->fieldHeight=height;
    texture->fieldVersion++;return true;
}

bool MicroFxSceneSetGpuTextureStage(MicroFxScene *scene,int handle,
                                    MicroFxGpuTextureStage stage)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||(stage!=MICROFX_GPU_TEXTURE_BACKGROUND&&
       stage!=MICROFX_GPU_TEXTURE_OVERLAY))return false;
    texture->stage=stage;return true;
}

bool MicroFxSceneSetGpuTextureBlend(MicroFxScene *scene,int handle,bool blend)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture)return false;texture->blend=blend;return true;
}

bool MicroFxSceneSetGpuTextureOpacity(MicroFxScene *scene,int handle,float opacity)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture||opacity<0.0f||opacity>1.0f)return false;
    texture->opacity=opacity;return true;
}

bool MicroFxSceneSetGpuTextureVisible(MicroFxScene *scene,int handle,bool visible)
{
    MicroFxGpuTexture *texture=GpuTexture(scene,handle);
    if(!texture)return false;texture->visible=visible;return true;
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

bool MicroFxSceneSetTextAntialias(MicroFxScene *scene, int handle, bool enabled)
{
    if ((handle & MICROFX_HANDLE_KIND_MASK) != MICROFX_HANDLE_TEXT) return false;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    if (index < 0 || index >= scene->textCount) return false;
    if (scene->text[index].antialias == enabled) return true;
    scene->text[index].antialias = enabled;
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
    if (kind == MICROFX_HANDLE_OUTLINE && index < scene->outlineCount) {
        scene->outline[index].color = color; scene->outlineDirty = true; return true;
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
    case MICROFX_HANDLE_OUTLINE:
        if (index < 0 || index >= scene->outlineCount) return false;
        scene->outline[index].opacity = opacity; scene->outlineDirty = true; return true;
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
    case MICROFX_HANDLE_OUTLINE:
        if (index < 0 || index >= scene->outlineCount) return false;
        if (scene->outline[index].visible == visible) return true;
        scene->outline[index].visible = visible; scene->outlineDirty = true; return true;
    default: return false;
    }
}

bool MicroFxSceneSetSdfForeground(MicroFxScene *scene, int handle,
                                  bool foreground)
{
    if ((handle & MICROFX_HANDLE_KIND_MASK) != MICROFX_HANDLE_SDF) return false;
    int index = handle & MICROFX_HANDLE_INDEX_MASK;
    if (index < 0 || index >= scene->sdfCount) return false;
    if (scene->sdf[index].foreground == foreground) return true;
    scene->sdf[index].foreground = foreground;
    scene->sdfDirty = true;
    return true;
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
