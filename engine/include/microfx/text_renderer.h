#ifndef MICROFX_TEXT_RENDERER_H
#define MICROFX_TEXT_RENDERER_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include "raylib.h"
#include "microfx/scene.h"

typedef struct {
    GLuint program;
    GLuint vertexBuffer;
    GLint viewportLocation;
    GLint textureLocation;
    Font font;
    int vertexCount;
} MicroFxTextRenderer;

bool MicroFxTextRendererInit(MicroFxTextRenderer *renderer, Font font);
void MicroFxTextRendererDraw(MicroFxTextRenderer *renderer, MicroFxScene *scene,
                            int width, int height);
void MicroFxTextRendererDestroy(MicroFxTextRenderer *renderer);

#endif
