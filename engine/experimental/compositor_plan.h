#ifndef MICROFX_COMPOSITOR_PLAN_H
#define MICROFX_COMPOSITOR_PLAN_H

#include <stdbool.h>

#define MICROFX_COMPOSITOR_MAX_LAYERS 16

typedef enum { MICROFX_LAYER_SCENE, MICROFX_LAYER_UI, MICROFX_LAYER_EFFECT } MicroFxLayerKind;
typedef enum { MICROFX_BLEND_NORMAL, MICROFX_BLEND_ADD, MICROFX_BLEND_MULTIPLY,
               MICROFX_BLEND_SCREEN } MicroFxBlendMode;
typedef enum { MICROFX_COMPOSE_GLES, MICROFX_COMPOSE_NATIVE_PLANE } MicroFxComposeRoute;

typedef struct {
    MicroFxLayerKind kind;
    MicroFxBlendMode blend;
    float opacity;
    float pixelDensity;
    bool hasEffects;
} MicroFxLayerRequest;

typedef struct {
    int overlayPlanes;
    bool scaling;
    bool globalAlpha;
    bool zPosition;
    bool dmaBufImport;
} MicroFxCompositorCapabilities;

typedef struct {
    MicroFxComposeRoute route[MICROFX_COMPOSITOR_MAX_LAYERS];
    int count;
    int nativePlaneCount;
    int glesLayerCount;
    int firstNativeLayer;
    bool needsGlesTarget;
} MicroFxCompositionPlan;

MicroFxCompositionPlan MicroFxPlanComposition(
    const MicroFxLayerRequest *layers, int count,
    MicroFxCompositorCapabilities capabilities);

#endif
