#ifndef MICROFX_MESH_RENDERER_H
#define MICROFX_MESH_RENDERER_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include "microfx/scene.h"

#define MICROFX_MESH_BATCH_SIZE 16
#define MICROFX_MAX_MESH_BATCHES MICROFX_MAX_MESH_ELEMENTS

typedef struct {
    GLuint id;
    GLint viewLocation;
    GLint projectionLocation;
    GLint positionScaleLocation;
    GLint rotationLocation;
    GLint colorLocation;
    GLint effectLocation;
    GLint timeLocation;
    int sceneShaderIndex;
} MicroFxMeshProgram;

typedef struct {
    int firstVertex;
    int vertexCount;
    int elementCount;
    int shaderIndex;
    int elementIndex[MICROFX_MESH_BATCH_SIZE];
} MicroFxMeshBatch;

typedef struct {
    GLuint vertexBuffer;
    MicroFxMeshProgram programs[MICROFX_MAX_MESH_SHADERS + 1];
    int programCount;
    int vertexCount;
    int batchCount;
    MicroFxMeshBatch batches[MICROFX_MAX_MESH_BATCHES];
} MicroFxMeshRenderer;

bool MicroFxMeshRendererInit(MicroFxMeshRenderer *renderer);
bool MicroFxMeshRendererDraw(MicroFxMeshRenderer *renderer, MicroFxScene *scene,
                            const float *view, const float *projection);
void MicroFxMeshRendererDestroy(MicroFxMeshRenderer *renderer);

#endif
