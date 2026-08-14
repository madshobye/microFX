#include "microfx/quality.h"

MicroFxDensityDecision MicroFxEvaluateDensity(
    const MicroFxDensityPolicy *policy,
    float averageFrameMs,
    float frameBudgetMs,
    float density,
    float minimumDensity,
    int *underBudgetSamples)
{
    if (!policy || !underBudgetSamples || frameBudgetMs <= 0.0f) {
        return MICROFX_DENSITY_KEEP;
    }
    if (averageFrameMs > frameBudgetMs*policy->downThreshold) {
        *underBudgetSamples = 0;
        return density > minimumDensity + 0.001f
             ? MICROFX_DENSITY_LOWER : MICROFX_DENSITY_KEEP;
    }
    if (averageFrameMs < frameBudgetMs*policy->upThreshold &&
        density < 0.999f) {
        *underBudgetSamples += 1;
        if (*underBudgetSamples >= policy->upSamples) {
            *underBudgetSamples = 0;
            return MICROFX_DENSITY_RAISE;
        }
        return MICROFX_DENSITY_KEEP;
    }
    *underBudgetSamples = 0;
    return MICROFX_DENSITY_KEEP;
}
