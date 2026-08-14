#ifndef MICROFX_TEXT_RENDERER_H
#define MICROFX_TEXT_RENDERER_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include "raylib.h"
#include "microfx/scene.h"

#define MICROFX_MAX_FONT_FACES 4

typedef struct {
    GLuint program;
    GLuint vertexBuffer;
    GLint viewportLocation;
    GLint textureLocation;
    Font fonts[MICROFX_MAX_FONT_FACES];
    bool fontOwned[MICROFX_MAX_FONT_FACES];
    char fontPaths[MICROFX_MAX_FONT_FACES][MICROFX_MAX_ASSET_PATH];
    int fontCount;
    int fontIndex[MICROFX_MAX_TEXT_ELEMENTS];
    int firstVertex[MICROFX_MAX_TEXT_ELEMENTS];
    int vertexCounts[MICROFX_MAX_TEXT_ELEMENTS];
    int vertexCount;
} MicroFxTextRenderer;

bool MicroFxTextRendererInit(MicroFxTextRenderer *renderer, Font font);
bool MicroFxTextRendererDraw(MicroFxTextRenderer *renderer, MicroFxScene *scene,
                             int width, int height);
void MicroFxTextRendererDestroy(MicroFxTextRenderer *renderer);

#endif
