#ifndef MICROFX_TILE_RENDERER_H
#define MICROFX_TILE_RENDERER_H

#include <GLES2/gl2.h>
#include <raylib.h>
#include "microfx/scene.h"

typedef struct {
    int generation;
    int decodedCount;
    Image staging;
    Texture2D active;
} MicroFxTileMapRenderState;

typedef struct {
    GLuint program;
    GLuint vertexBuffer;
    GLint textureLocation;
    GLint grayscaleLocation;
    GLint contrastLocation;
    GLint brightnessLocation;
    GLint invertLocation;
    GLint tintLocation;
    MicroFxTileMapRenderState maps[MICROFX_MAX_TILE_MAPS];
} MicroFxTileRenderer;

bool MicroFxTileRendererInit(MicroFxTileRenderer *renderer);
bool MicroFxTileRendererUpdate(MicroFxTileRenderer *renderer,
                               MicroFxScene *scene);
void MicroFxTileRendererDraw(MicroFxTileRenderer *renderer,
                             const MicroFxScene *scene);
void MicroFxTileRendererDestroy(MicroFxTileRenderer *renderer,
                                MicroFxScene *scene);

#endif
