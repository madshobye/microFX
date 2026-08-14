#include "compositor_plan.h"

static bool NativePlaneEligible(const MicroFxLayerRequest *layer,
                                MicroFxCompositorCapabilities caps)
{
    if (layer->kind != MICROFX_LAYER_UI || layer->hasEffects) return false;
    if (layer->blend != MICROFX_BLEND_NORMAL) return false;
    if (layer->pixelDensity != 1.0f && !caps.scaling) return false;
    if (layer->opacity < 1.0f && !caps.globalAlpha) return false;
    return caps.dmaBufImport && caps.zPosition;
}

MicroFxCompositionPlan MicroFxPlanComposition(
    const MicroFxLayerRequest *layers, int count,
    MicroFxCompositorCapabilities capabilities)
{
    MicroFxCompositionPlan plan = { .firstNativeLayer = -1 };
    if (!layers || count <= 0) return plan;
    if (count > MICROFX_COMPOSITOR_MAX_LAYERS) count = MICROFX_COMPOSITOR_MAX_LAYERS;
    plan.count = count;

    /*
     * A single GLES target collapses every GLES-routed layer into one plane.
     * Native planes can therefore represent only a topmost contiguous suffix
     * of the requested stack. Choosing an eligible lower layer and then
     * returning a later effect to GLES would reverse their visual order.
     *
     * Walk from front to back and stop assigning planes at the first layer
     * that cannot be represented natively (including plane exhaustion). The
     * remaining lower layers are safely flattened into the GLES target.
     */
    bool nativeSuffix = capabilities.overlayPlanes > 0;
    for (int index = count - 1; index >= 0; index--) {
        bool native = nativeSuffix &&
                      plan.nativePlaneCount < capabilities.overlayPlanes &&
                      NativePlaneEligible(&layers[index], capabilities);
        if (native) {
            plan.route[index] = MICROFX_COMPOSE_NATIVE_PLANE;
            plan.nativePlaneCount++;
            plan.firstNativeLayer = index;
        } else {
            nativeSuffix = false;
            plan.route[index] = MICROFX_COMPOSE_GLES;
            plan.glesLayerCount++;
            plan.needsGlesTarget = true;
        }
    }
    return plan;
}
