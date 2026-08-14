#ifndef MICROFX_QUAD_RENDERER_H
#define MICROFX_QUAD_RENDERER_H

#include <GLES2/gl2.h>
#include "microfx/scene.h"

typedef struct {
    GLuint program;
    GLuint vertexBuffers[3];
    unsigned activeVertexBuffer;
    GLint viewportLocation;
    int backgroundVertexCount;
    int vertexCount;
    bool backgroundOpaque;
} MicroFxQuadRenderer;

bool MicroFxQuadRendererInit(MicroFxQuadRenderer *renderer);
void MicroFxQuadRendererDraw(MicroFxQuadRenderer *renderer, MicroFxScene *scene,
                            int width, int height, bool background);
void MicroFxQuadRendererDestroy(MicroFxQuadRenderer *renderer);

#endif
