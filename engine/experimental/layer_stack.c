#include "layer_stack.h"

#include <string.h>

static bool RequestIsValid(MicroFxLayerRequest request)
{
    return request.kind >= MICROFX_LAYER_SCENE &&
           request.kind <= MICROFX_LAYER_EFFECT &&
           request.blend >= MICROFX_BLEND_NORMAL &&
           request.blend <= MICROFX_BLEND_SCREEN &&
           request.opacity >= 0.0f && request.opacity <= 1.0f &&
           request.pixelDensity >= 0.25f && request.pixelDensity <= 1.0f;
}

static bool NameIsUnique(const MicroFxLayerStack *stack, const char *name)
{
    for (int index = 0; index < stack->count; index++) {
        if (strcmp(stack->layer[index].name, name) == 0) return false;
    }
    return true;
}

static MicroFxLayerDescriptor *FindLayer(MicroFxLayerStack *stack, int layerId)
{
    if (!stack || layerId < 0 || layerId >= stack->count) return NULL;
    return &stack->layer[layerId];
}

void MicroFxLayerStackInit(MicroFxLayerStack *stack)
{
    if (stack) memset(stack, 0, sizeof(*stack));
}

int MicroFxLayerStackAdd(MicroFxLayerStack *stack, const char *name,
                         int logicalWidth, int logicalHeight,
                         MicroFxLayerOrigin origin,
                         MicroFxLayerRequest request)
{
    if (!stack || !name || !name[0] ||
        strlen(name) >= MICROFX_LAYER_NAME_CAPACITY ||
        stack->count >= MICROFX_COMPOSITOR_MAX_LAYERS ||
        logicalWidth <= 0 || logicalHeight <= 0 ||
        (origin != MICROFX_LAYER_ORIGIN_TOP_LEFT &&
         origin != MICROFX_LAYER_ORIGIN_BOTTOM_LEFT) ||
        !RequestIsValid(request) || !NameIsUnique(stack, name)) {
        return -1;
    }

    int id = stack->count++;
    MicroFxLayerDescriptor *layer = &stack->layer[id];
    layer->id = id;
    memcpy(layer->name, name, strlen(name) + 1);
    layer->logicalWidth = logicalWidth;
    layer->logicalHeight = logicalHeight;
    layer->origin = origin;
    layer->request = request;
    return id;
}

bool MicroFxLayerStackSetRequest(MicroFxLayerStack *stack, int layerId,
                                 MicroFxLayerRequest request)
{
    MicroFxLayerDescriptor *layer = FindLayer(stack, layerId);
    if (!layer || !RequestIsValid(request)) return false;
    layer->request = request;
    return true;
}

bool MicroFxLayerStackSetMemberCount(MicroFxLayerStack *stack, int layerId,
                                     int memberCount)
{
    MicroFxLayerDescriptor *layer = FindLayer(stack, layerId);
    if (!layer || memberCount < 0) return false;
    layer->memberCount = memberCount;
    return true;
}

MicroFxCompositionPlan MicroFxPlanLayerStack(
    const MicroFxLayerStack *stack,
    MicroFxCompositorCapabilities capabilities)
{
    MicroFxLayerRequest request[MICROFX_COMPOSITOR_MAX_LAYERS];
    if (!stack || stack->count <= 0) {
        return MicroFxPlanComposition(NULL, 0, capabilities);
    }
    for (int index = 0; index < stack->count; index++)
        request[index] = stack->layer[index].request;
    return MicroFxPlanComposition(request, stack->count, capabilities);
}
