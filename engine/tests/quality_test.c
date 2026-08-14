#include "microfx/quality.h"
#include <assert.h>

int main(void)
{
    MicroFxDensityPolicy policy = { 1.08f, 0.72f, 3 };
    int stable = 0;
    assert(MicroFxEvaluateDensity(&policy, 37, 33, 1, .5f, &stable) ==
           MICROFX_DENSITY_LOWER);
    assert(stable == 0);
    assert(MicroFxEvaluateDensity(&policy, 37, 33, .5f, .5f, &stable) ==
           MICROFX_DENSITY_KEEP);
    assert(MicroFxEvaluateDensity(&policy, 20, 33, .75f, .5f, &stable) ==
           MICROFX_DENSITY_KEEP && stable == 1);
    assert(MicroFxEvaluateDensity(&policy, 20, 33, .75f, .5f, &stable) ==
           MICROFX_DENSITY_KEEP && stable == 2);
    assert(MicroFxEvaluateDensity(&policy, 20, 33, .75f, .5f, &stable) ==
           MICROFX_DENSITY_RAISE && stable == 0);
    stable = 2;
    assert(MicroFxEvaluateDensity(&policy, 27, 33, .75f, .5f, &stable) ==
           MICROFX_DENSITY_KEEP && stable == 0);
    return 0;
}
