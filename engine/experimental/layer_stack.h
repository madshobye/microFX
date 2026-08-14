#ifndef MICROFX_LAYER_STACK_H
#define MICROFX_LAYER_STACK_H

#include "compositor_plan.h"

#define MICROFX_LAYER_NAME_CAPACITY 48

typedef enum {
    MICROFX_LAYER_ORIGIN_TOP_LEFT,
    MICROFX_LAYER_ORIGIN_BOTTOM_LEFT
} MicroFxLayerOrigin;

typedef struct {
    int id;
    char name[MICROFX_LAYER_NAME_CAPACITY];
    int logicalWidth;
    int logicalHeight;
    MicroFxLayerOrigin origin;
    MicroFxLayerRequest request;
    int memberCount;
} MicroFxLayerDescriptor;

typedef struct {
    MicroFxLayerDescriptor layer[MICROFX_COMPOSITOR_MAX_LAYERS];
    int count;
} MicroFxLayerStack;

void MicroFxLayerStackInit(MicroFxLayerStack *stack);

int MicroFxLayerStackAdd(MicroFxLayerStack *stack, const char *name,
                         int logicalWidth, int logicalHeight,
                         MicroFxLayerOrigin origin,
                         MicroFxLayerRequest request);

bool MicroFxLayerStackSetRequest(MicroFxLayerStack *stack, int layerId,
                                 MicroFxLayerRequest request);

bool MicroFxLayerStackSetMemberCount(MicroFxLayerStack *stack, int layerId,
                                     int memberCount);

MicroFxCompositionPlan MicroFxPlanLayerStack(
    const MicroFxLayerStack *stack,
    MicroFxCompositorCapabilities capabilities);

#endif
