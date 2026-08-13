#ifndef MICROFX_SCENE_H
#define MICROFX_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#define MICROFX_MAX_SDF_ELEMENTS 256
#define MICROFX_MAX_QUAD_ELEMENTS 256
#define MICROFX_MAX_MESH_ELEMENTS 16
#define MICROFX_MAX_TEXT_ELEMENTS 32
#define MICROFX_MAX_TEXT_BYTES 128
#define MICROFX_MAX_ASSET_PATH 256

#define MICROFX_HANDLE_SDF  0x01000000
#define MICROFX_HANDLE_MESH 0x02000000
#define MICROFX_HANDLE_TEXT 0x03000000
#define MICROFX_HANDLE_QUAD 0x04000000
#define MICROFX_HANDLE_KIND_MASK 0xff000000
#define MICROFX_HANDLE_INDEX_MASK 0x00ffffff

typedef enum {
    MICROFX_SDF_CIRCLE = 0,
    MICROFX_SDF_ROUNDED_RECT = 1,
    MICROFX_SDF_RECT = 2
} MicroFxSdfKind;

typedef struct {
    MicroFxSdfKind kind;
    float x;
    float y;
    float width;
    float height;
    float radius;
    float rotation;
    uint32_t color;
    bool visible;
} MicroFxSdfElement;

typedef enum {
    MICROFX_QUAD_RECT = 0,
    MICROFX_QUAD_CIRCLE = 1
} MicroFxQuadKind;

typedef struct {
    MicroFxQuadKind kind;
    float x;
    float y;
    float width;
    float height;
    float rotation;
    uint32_t topColor;
    uint32_t bottomColor;
    bool background;
    bool visible;
} MicroFxQuadElement;

typedef enum {
    MICROFX_MESH_CUBE = 0,
    MICROFX_MESH_SPHERE = 1,
    MICROFX_MESH_MODEL = 2,
    MICROFX_MESH_WIRE_CUBE = 3,
    MICROFX_MESH_GRID = 4
} MicroFxMeshKind;

typedef struct {
    MicroFxMeshKind kind;
    float position[3];
    float rotation[3];
    float scale;
    uint32_t color;
    float effect[3];
    char assetPath[MICROFX_MAX_ASSET_PATH];
    bool visible;
} MicroFxMeshElement;

typedef struct {
    char text[MICROFX_MAX_TEXT_BYTES];
    float x;
    float y;
    float size;
    uint32_t color;
    bool visible;
} MicroFxTextElement;

typedef struct {
    float position[3];
    float target[3];
    float fovY;
} MicroFxCamera;

typedef struct {
    int outputWidth;
    int outputHeight;
    int targetFps;
    float pixelDensity;
    float minimumPixelDensity;
    float durationSeconds;
    bool automaticDensity;
    bool debugBar;
    bool configured;
} MicroFxRuntimeSettings;

typedef struct {
    MicroFxSdfElement sdf[MICROFX_MAX_SDF_ELEMENTS];
    int sdfCount;
    bool sdfDirty;
    MicroFxQuadElement quad[MICROFX_MAX_QUAD_ELEMENTS];
    int quadCount;
    bool quadDirty;
    MicroFxMeshElement mesh[MICROFX_MAX_MESH_ELEMENTS];
    int meshCount;
    bool meshGeometryDirty;
    bool meshStateDirty;
    MicroFxTextElement text[MICROFX_MAX_TEXT_ELEMENTS];
    int textCount;
    bool textDirty;
    MicroFxCamera camera;
    MicroFxRuntimeSettings runtime;
    float clearColor[3];
    float time;
} MicroFxScene;

void MicroFxSceneInit(MicroFxScene *scene);
int MicroFxSceneAddCircle(MicroFxScene *scene, float x, float y, float radius,
                         uint32_t color);
int MicroFxSceneAddRoundedRect(MicroFxScene *scene, float x, float y,
                              float width, float height, float radius,
                              uint32_t color);
int MicroFxSceneAddRect(MicroFxScene *scene, float x, float y,
                       float width, float height, uint32_t color);
int MicroFxSceneAddFastCircle(MicroFxScene *scene, float x, float y,
                             float radius, uint32_t color);
int MicroFxSceneAddGradientRect(MicroFxScene *scene, float x, float y,
                               float width, float height, uint32_t topColor,
                               uint32_t bottomColor);
int MicroFxSceneAddBackground(MicroFxScene *scene, uint32_t topColor,
                             uint32_t bottomColor);
bool MicroFxSceneMove(MicroFxScene *scene, int handle, float x, float y,
                     float rotation);
int MicroFxSceneAddCube(MicroFxScene *scene, float x, float y, float z,
                       float scale, uint32_t color);
int MicroFxSceneAddSphere(MicroFxScene *scene, float x, float y, float z,
                         float scale, uint32_t color);
int MicroFxSceneAddWireCube(MicroFxScene *scene, float x, float y, float z,
                           float scale, uint32_t color);
int MicroFxSceneAddGrid(MicroFxScene *scene, float x, float y, float z,
                       float scale, uint32_t color);
int MicroFxSceneAddModel(MicroFxScene *scene, const char *assetPath,
                        float x, float y, float z, float scale,
                        uint32_t color);
bool MicroFxSceneTransform(MicroFxScene *scene, int handle, float x, float y,
                          float z, float rx, float ry, float rz, float scale);
int MicroFxSceneAddText(MicroFxScene *scene, const char *text, float x, float y,
                       float size, uint32_t color);
bool MicroFxSceneSetText(MicroFxScene *scene, int handle, const char *text);
bool MicroFxSceneSetColor(MicroFxScene *scene, int handle, uint32_t color);
bool MicroFxSceneSetEffect(MicroFxScene *scene, int handle, int effect,
                          float amount, float scale);
void MicroFxSceneSetCamera(MicroFxScene *scene, float x, float y, float z,
                          float targetX, float targetY, float targetZ,
                          float fovY);

#endif
