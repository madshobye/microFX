#ifndef MICROFX_OUTLINE_RENDERER_H
#define MICROFX_OUTLINE_RENDERER_H

#include <GLES2/gl2.h>
#include "microfx/scene.h"

typedef struct {
    GLuint program;
    GLuint vertexBuffers[3];
    unsigned int activeVertexBuffer;
    GLint viewportLocation;
    int vertexCount;
} MicroFxOutlineRenderer;

bool MicroFxOutlineRendererInit(MicroFxOutlineRenderer *renderer);
void MicroFxOutlineRendererDraw(MicroFxOutlineRenderer *renderer,
                                MicroFxScene *scene, int width, int height);
void MicroFxOutlineRendererDestroy(MicroFxOutlineRenderer *renderer);

#endif
