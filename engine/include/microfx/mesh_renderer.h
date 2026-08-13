#ifndef MICROFX_MESH_RENDERER_H
#define MICROFX_MESH_RENDERER_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include "microfx/scene.h"

typedef struct {
    GLuint program;
    GLuint vertexBuffer;
    GLint viewLocation;
    GLint projectionLocation;
    GLint positionScaleLocation;
    GLint rotationLocation;
    GLint colorLocation;
    GLint effectLocation;
    GLint timeLocation;
    int vertexCount;
} MicroFxMeshRenderer;

bool MicroFxMeshRendererInit(MicroFxMeshRenderer *renderer);
bool MicroFxMeshRendererDraw(MicroFxMeshRenderer *renderer, MicroFxScene *scene,
                            const float *view, const float *projection);
void MicroFxMeshRendererDestroy(MicroFxMeshRenderer *renderer);

#endif
