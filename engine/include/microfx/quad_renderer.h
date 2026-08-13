#ifndef MICROFX_QUAD_RENDERER_H
#define MICROFX_QUAD_RENDERER_H

#include <GLES2/gl2.h>
#include "microfx/scene.h"

typedef struct {
    GLuint program;
    GLuint vertexBuffer;
    GLint viewportLocation;
    int backgroundVertexCount;
    int vertexCount;
} MicroFxQuadRenderer;

bool MicroFxQuadRendererInit(MicroFxQuadRenderer *renderer);
void MicroFxQuadRendererDraw(MicroFxQuadRenderer *renderer, MicroFxScene *scene,
                            int width, int height, bool background);
void MicroFxQuadRendererDestroy(MicroFxQuadRenderer *renderer);

#endif
