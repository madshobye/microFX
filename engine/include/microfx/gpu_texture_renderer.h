#ifndef MICROFX_GPU_TEXTURE_RENDERER_H
#define MICROFX_GPU_TEXTURE_RENDERER_H

#include <GLES2/gl2.h>
#include <raylib.h>
#include "microfx/scene.h"
#include "microfx/tile_renderer.h"

typedef struct {
    GLuint program;
    GLuint fieldTexture;
    Texture2D assetTexture;
    Texture2D secondaryAssetTexture;
    Texture2D tertiaryAssetTexture;
    int shaderVersion;
    int secondaryVersion;
    int tertiaryVersion;
    int fieldVersion;
    bool assetLoaded;
    GLint textureLocation;
    GLint secondaryTextureLocation;
    GLint tertiaryTextureLocation;
    GLint fieldLocation;
    GLint fieldSizeLocation;
    GLint resolutionLocation;
    GLint timeLocation;
    GLint paramsLocation;
    GLuint cachedTexture;
    GLuint cachedFramebuffer;
    int cachedWidth;
    int cachedHeight;
    int cachedShaderVersion;
    int cachedParamVersion;
    int cachedFieldVersion;
    GLuint cachedSourceId;
    GLuint cachedSecondaryId;
    GLuint cachedTertiaryId;
    bool cacheValid;
} MicroFxGpuTextureRenderState;

typedef struct {
    GLuint defaultProgram;
    GLuint cachedProgram;
    GLuint vertexBuffer;
    GLint defaultTextureLocation;
    GLint cachedTextureLocation;
    GLint cachedOpacityLocation;
    MicroFxGpuTextureRenderState textures[MICROFX_MAX_GPU_TEXTURES];
} MicroFxGpuTextureRenderer;

bool MicroFxGpuTextureRendererInit(MicroFxGpuTextureRenderer *renderer);
bool MicroFxGpuTextureRendererUpdate(MicroFxGpuTextureRenderer *renderer,
                                     MicroFxScene *scene);
void MicroFxGpuTextureRendererDraw(MicroFxGpuTextureRenderer *renderer,
                                   const MicroFxTileRenderer *tiles,
                                   const MicroFxScene *scene,
                                   MicroFxGpuTextureStage stage,
                                   int width,int height);
void MicroFxGpuTextureRendererDestroy(MicroFxGpuTextureRenderer *renderer,
                                      MicroFxScene *scene);

#endif
