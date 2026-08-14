#ifndef MICROFX_IMAGE_RENDERER_H
#define MICROFX_IMAGE_RENDERER_H

#include <GLES2/gl2.h>
#include <raylib.h>
#include "microfx/scene.h"

typedef struct {
    GLuint program;
    GLuint vertexBuffer;
    GLint viewportLocation;
    GLint textureLocation;
    GLint transformLocation;
    GLint colorLocation;
    Texture2D textures[MICROFX_MAX_IMAGE_ELEMENTS];
    char texturePaths[MICROFX_MAX_IMAGE_ELEMENTS][MICROFX_MAX_ASSET_PATH];
    int textureIndex[MICROFX_MAX_IMAGE_ELEMENTS];
    int textureCount;
} MicroFxImageRenderer;

bool MicroFxImageRendererInit(MicroFxImageRenderer *renderer);
bool MicroFxImageRendererDraw(MicroFxImageRenderer *renderer,
                              MicroFxScene *scene, int width, int height,
                              bool background);
void MicroFxImageRendererDestroy(MicroFxImageRenderer *renderer);

#endif
