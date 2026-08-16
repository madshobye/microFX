#ifndef MICROFX_SDF_RENDERER_H
#define MICROFX_SDF_RENDERER_H

#include <GLES2/gl2.h>
#include "microfx/scene.h"

typedef struct {
    GLuint program;
    GLuint vertexBuffer;
    GLint viewportLocation;
    int vertexFirst[2];
    int vertexCount[2];
} MicroFxSdfRenderer;

bool MicroFxSdfRendererInit(MicroFxSdfRenderer *renderer);
void MicroFxSdfRendererDraw(MicroFxSdfRenderer *renderer, MicroFxScene *scene,
                           int width, int height, bool foreground);
void MicroFxSdfRendererDestroy(MicroFxSdfRenderer *renderer);

#endif
