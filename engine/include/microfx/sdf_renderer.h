#ifndef MICROFX_SDF_RENDERER_H
#define MICROFX_SDF_RENDERER_H

#include <GLES2/gl2.h>
#include "microfx/scene.h"

typedef struct {
    GLuint program;
    GLuint vertexBuffer;
    GLint viewportLocation;
    int vertexCount;
} MicroFxSdfRenderer;

bool MicroFxSdfRendererInit(MicroFxSdfRenderer *renderer);
void MicroFxSdfRendererDraw(MicroFxSdfRenderer *renderer, MicroFxScene *scene,
                           int width, int height);
void MicroFxSdfRendererDestroy(MicroFxSdfRenderer *renderer);

#endif
