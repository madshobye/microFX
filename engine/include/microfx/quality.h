#ifndef MICROFX_QUALITY_H
#define MICROFX_QUALITY_H

typedef enum {
    MICROFX_DENSITY_KEEP = 0,
    MICROFX_DENSITY_LOWER = -1,
    MICROFX_DENSITY_RAISE = 1
} MicroFxDensityDecision;

typedef struct {
    float downThreshold;
    float upThreshold;
    int upSamples;
} MicroFxDensityPolicy;

MicroFxDensityDecision MicroFxEvaluateDensity(
    const MicroFxDensityPolicy *policy,
    float averageFrameMs,
    float frameBudgetMs,
    float density,
    float minimumDensity,
    int *underBudgetSamples);

#endif
