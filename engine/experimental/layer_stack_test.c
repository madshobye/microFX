#include "layer_stack.h"

#include <assert.h>
#include <stdio.h>

static MicroFxLayerRequest Request(MicroFxLayerKind kind, float density)
{
    MicroFxLayerRequest request = {
        .kind = kind,
        .blend = MICROFX_BLEND_NORMAL,
        .opacity = 1.0f,
        .pixelDensity = density,
        .hasEffects = false
    };
    return request;
}

int main(void)
{
    MicroFxLayerStack stack;
    MicroFxLayerStackInit(&stack);

    int scene = MicroFxLayerStackAdd(
        &stack, "scene", 1920, 1080, MICROFX_LAYER_ORIGIN_TOP_LEFT,
        Request(MICROFX_LAYER_SCENE, 0.5f));
    int ui = MicroFxLayerStackAdd(
        &stack, "ui", 1920, 1080, MICROFX_LAYER_ORIGIN_TOP_LEFT,
        Request(MICROFX_LAYER_UI, 1.0f));
    assert(scene == 0 && ui == 1 && stack.count == 2);
    assert(stack.layer[scene].logicalWidth == 1920);
    assert(stack.layer[scene].origin == MICROFX_LAYER_ORIGIN_TOP_LEFT);

    assert(MicroFxLayerStackAdd(
        &stack, "ui", 1920, 1080, MICROFX_LAYER_ORIGIN_TOP_LEFT,
        Request(MICROFX_LAYER_UI, 1.0f)) == -1);
    assert(MicroFxLayerStackAdd(
        &stack, "bad-density", 1920, 1080, MICROFX_LAYER_ORIGIN_TOP_LEFT,
        Request(MICROFX_LAYER_UI, 0.1f)) == -1);
    assert(MicroFxLayerStackAdd(
        &stack, "bad-size", 0, 1080, MICROFX_LAYER_ORIGIN_TOP_LEFT,
        Request(MICROFX_LAYER_UI, 1.0f)) == -1);

    assert(MicroFxLayerStackSetMemberCount(&stack, scene, 14));
    assert(stack.layer[scene].memberCount == 14);
    assert(!MicroFxLayerStackSetMemberCount(&stack, 99, 1));

    MicroFxLayerRequest uiRequest = stack.layer[ui].request;
    uiRequest.opacity = 0.75f;
    assert(MicroFxLayerStackSetRequest(&stack, ui, uiRequest));
    uiRequest.pixelDensity = 2.0f;
    assert(!MicroFxLayerStackSetRequest(&stack, ui, uiRequest));

    MicroFxCompositorCapabilities capabilities = {
        .overlayPlanes = 1,
        .scaling = true,
        .globalAlpha = true,
        .zPosition = true,
        .dmaBufImport = true
    };
    MicroFxCompositionPlan plan = MicroFxPlanLayerStack(&stack, capabilities);
    assert(plan.count == 2);
    assert(plan.route[scene] == MICROFX_COMPOSE_GLES);
    assert(plan.route[ui] == MICROFX_COMPOSE_NATIVE_PLANE);
    assert(plan.firstNativeLayer == ui);

    MicroFxLayerStackInit(&stack);
    plan = MicroFxPlanLayerStack(&stack, capabilities);
    assert(plan.count == 0 && plan.firstNativeLayer == -1);

    puts("experimental layer stack tests passed");
    return 0;
}
