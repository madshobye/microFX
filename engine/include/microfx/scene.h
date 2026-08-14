#ifndef MICROFX_SCENE_H
#define MICROFX_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#define MICROFX_DESIGN_WIDTH 1920
#define MICROFX_DESIGN_HEIGHT 1080

#define MICROFX_MAX_SDF_ELEMENTS 256
#define MICROFX_MAX_QUAD_ELEMENTS 512
#define MICROFX_MAX_MESH_ELEMENTS 256
#define MICROFX_MAX_TEXT_ELEMENTS 64
#define MICROFX_MAX_IMAGE_ELEMENTS 16
#define MICROFX_MAX_TEXT_BYTES 128
#define MICROFX_MAX_ASSET_PATH 256
#define MICROFX_MAX_MESH_SHADERS 8

#define MICROFX_HANDLE_SDF  0x01000000
#define MICROFX_HANDLE_MESH 0x02000000
#define MICROFX_HANDLE_TEXT 0x03000000
#define MICROFX_HANDLE_QUAD 0x04000000
#define MICROFX_HANDLE_IMAGE 0x05000000
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
    float opacity;
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
    float opacity;
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
    int shaderIndex;
    char assetPath[MICROFX_MAX_ASSET_PATH];
    bool visible;
} MicroFxMeshElement;

typedef struct {
    char vertexPath[MICROFX_MAX_ASSET_PATH];
    char fragmentPath[MICROFX_MAX_ASSET_PATH];
} MicroFxMeshShader;

typedef struct {
    char text[MICROFX_MAX_TEXT_BYTES];
    char fontPath[MICROFX_MAX_ASSET_PATH];
    float x;
    float y;
    float size;
    float rotation;
    uint32_t color;
    float opacity;
    bool antialias;
    bool visible;
} MicroFxTextElement;

typedef struct {
    char assetPath[MICROFX_MAX_ASSET_PATH];
    float x;
    float y;
    float scale;
    float rotation;
    uint32_t tint;
    float opacity;
    bool visible;
    bool background;
} MicroFxImageElement;

typedef struct {
    float position[3];
    float target[3];
    float fovY;
} MicroFxCamera;

typedef enum {
    MICROFX_COLOR_RGB565 = 0,
    MICROFX_COLOR_RGBA8888 = 1
} MicroFxColorFormat;

typedef enum {
    MICROFX_ANTIALIAS_NONE = 0,
    MICROFX_ANTIALIAS_MSAA4 = 1
} MicroFxAntialiasing;

typedef struct {
    int outputWidth;
    int outputHeight;
    int targetFps;
    float pixelDensity;
    float minimumPixelDensity;
    float durationSeconds;
    MicroFxColorFormat colorFormat;
    MicroFxAntialiasing antialiasing;
    int depthBits;
    bool automaticDensity;
    bool dithering;
    // 0 disables diagnostics, a negative value keeps them visible, and a
    // positive value is the absolute runtime second when they disappear.
    float debugBarUntilSeconds;
    bool profiling;
    int profileIntervalFrames;
    int densitySampleFrames;
    float densityStep;
    float densityDownThreshold;
    float densityUpThreshold;
    int densityUpSamples;
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
    MicroFxMeshShader meshShader[MICROFX_MAX_MESH_SHADERS];
    int meshShaderCount;
    bool meshGeometryDirty;
    bool meshStateDirty;
    MicroFxTextElement text[MICROFX_MAX_TEXT_ELEMENTS];
    int textCount;
    bool textDirty;
    MicroFxImageElement image[MICROFX_MAX_IMAGE_ELEMENTS];
    int imageCount;
    bool imageDirty;
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
bool MicroFxSceneHasOpaqueCoveringBackground(const MicroFxScene *scene);
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
int MicroFxSceneAddImage(MicroFxScene *scene, const char *assetPath,
                        float x, float y, float scale, uint32_t tint);
int MicroFxSceneAddBackgroundImage(MicroFxScene *scene, const char *assetPath,
                                  uint32_t tint);
bool MicroFxSceneSetImageScale(MicroFxScene *scene, int handle, float scale);
bool MicroFxSceneSetText(MicroFxScene *scene, int handle, const char *text);
bool MicroFxSceneSetTextFont(MicroFxScene *scene, int handle,
                             const char *assetPath);
bool MicroFxSceneSetTextAntialias(MicroFxScene *scene, int handle, bool enabled);
bool MicroFxSceneSetColor(MicroFxScene *scene, int handle, uint32_t color);
bool MicroFxSceneSetOpacity(MicroFxScene *scene, int handle, float opacity);
bool MicroFxSceneSetVisible(MicroFxScene *scene, int handle, bool visible);
bool MicroFxSceneSetEffect(MicroFxScene *scene, int handle, int effect,
                          float amount, float scale);
bool MicroFxSceneSetMeshShader(MicroFxScene *scene, int handle,
                               const char *vertexPath,
                               const char *fragmentPath);
void MicroFxSceneSetCamera(MicroFxScene *scene, float x, float y, float z,
                          float targetX, float targetY, float targetZ,
                          float fovY);

#endif
