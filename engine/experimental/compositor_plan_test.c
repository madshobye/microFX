#include "compositor_plan.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* A later GLES effect means the lower UI cannot be lifted above it. */
    const MicroFxLayerRequest layers[] = {
        { MICROFX_LAYER_SCENE, MICROFX_BLEND_NORMAL, 1.0f, 0.5f, false },
        { MICROFX_LAYER_UI, MICROFX_BLEND_NORMAL, 1.0f, 1.0f, false },
        { MICROFX_LAYER_UI, MICROFX_BLEND_ADD, 0.7f, 1.0f, false },
        { MICROFX_LAYER_EFFECT, MICROFX_BLEND_NORMAL, 1.0f, 1.0f, true }
    };
    MicroFxCompositorCapabilities capable = {
        .overlayPlanes = 2, .scaling = true, .globalAlpha = true,
        .zPosition = true, .dmaBufImport = true
    };
    MicroFxCompositionPlan plan = MicroFxPlanComposition(layers, 4, capable);
    assert(plan.count == 4);
    for (int index = 0; index < plan.count; index++)
        assert(plan.route[index] == MICROFX_COMPOSE_GLES);
    assert(plan.nativePlaneCount == 0);
    assert(plan.glesLayerCount == 4);
    assert(plan.firstNativeLayer == -1);
    assert(plan.needsGlesTarget);

    /* Native UI is safe when it is the topmost suffix above the GLES scene. */
    const MicroFxLayerRequest sceneAndUi[] = {
        { MICROFX_LAYER_SCENE, MICROFX_BLEND_NORMAL, 1.0f, 0.5f, false },
        { MICROFX_LAYER_UI, MICROFX_BLEND_NORMAL, 1.0f, 1.0f, false },
        { MICROFX_LAYER_UI, MICROFX_BLEND_NORMAL, 0.8f, 1.0f, false }
    };
    plan = MicroFxPlanComposition(sceneAndUi, 3, capable);
    assert(plan.route[0] == MICROFX_COMPOSE_GLES);
    assert(plan.route[1] == MICROFX_COMPOSE_NATIVE_PLANE);
    assert(plan.route[2] == MICROFX_COMPOSE_NATIVE_PLANE);
    assert(plan.nativePlaneCount == 2);
    assert(plan.glesLayerCount == 1);
    assert(plan.firstNativeLayer == 1);
    assert(plan.needsGlesTarget);

    /* On plane exhaustion, preserve the highest layers and flatten below. */
    capable.overlayPlanes = 1;
    plan = MicroFxPlanComposition(sceneAndUi, 3, capable);
    assert(plan.route[0] == MICROFX_COMPOSE_GLES);
    assert(plan.route[1] == MICROFX_COMPOSE_GLES);
    assert(plan.route[2] == MICROFX_COMPOSE_NATIVE_PLANE);
    assert(plan.nativePlaneCount == 1);
    assert(plan.glesLayerCount == 2);
    assert(plan.firstNativeLayer == 2);

    /* A fully native stack does not allocate an otherwise empty GLES target. */
    const MicroFxLayerRequest uiOnly[] = {
        { MICROFX_LAYER_UI, MICROFX_BLEND_NORMAL, 1.0f, 1.0f, false },
        { MICROFX_LAYER_UI, MICROFX_BLEND_NORMAL, 1.0f, 1.0f, false }
    };
    capable.overlayPlanes = 2;
    plan = MicroFxPlanComposition(uiOnly, 2, capable);
    assert(plan.nativePlaneCount == 2);
    assert(plan.glesLayerCount == 0);
    assert(plan.firstNativeLayer == 0);
    assert(!plan.needsGlesTarget);

    capable.dmaBufImport = false;
    plan = MicroFxPlanComposition(layers, 4, capable);
    assert(plan.nativePlaneCount == 0 && plan.glesLayerCount == 4 && plan.needsGlesTarget);
    for (int index = 0; index < plan.count; index++) assert(plan.route[index] == MICROFX_COMPOSE_GLES);

    plan = MicroFxPlanComposition(NULL, 3, capable);
    assert(plan.count == 0 && plan.firstNativeLayer == -1);
    puts("experimental compositor planner tests passed");
    return 0;
}
