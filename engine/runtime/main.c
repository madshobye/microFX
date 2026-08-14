// Native microFX runtime. Applications live in apps/ as JavaScript and assets.
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "microfx/scene.h"
#include "microfx/quality.h"
#include "microfx/script.h"
#include "microfx/identity.h"
#include "microfx/sdf_renderer.h"
#include "microfx/quad_renderer.h"
#include "microfx/mesh_renderer.h"
#include "microfx/text_renderer.h"
#include "microfx/image_renderer.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DESIGN_WIDTH MICROFX_DESIGN_WIDTH
#define DESIGN_HEIGHT MICROFX_DESIGN_HEIGHT
#define DEFAULT_OUTPUT_WIDTH 1280
#define DEFAULT_OUTPUT_HEIGHT 720
typedef struct {
    int outputWidth;
    int outputHeight;
    int nativeWidth;
    int nativeHeight;
    int targetFps;
    float pixelDensity;
    float minimumPixelDensity;
    MicroFxColorFormat colorFormat;
    MicroFxAntialiasing antialiasing;
    int depthBits;
    bool automaticDensity;
    bool dithering;
    bool resolutionFixed;
    bool profiling;
    int profileIntervalFrames;
    int densitySampleFrames;
    float densityStep;
    float densityDownThreshold;
    float densityUpThreshold;
    int densityUpSamples;
} RuntimeConfig;

static void ApplyConfigFile(RuntimeConfig *config, const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) return;
    char line[160];
    while (fgets(line, sizeof(line), file) != NULL) {
        char key[80] = { 0 };
        char value[80] = { 0 };
        if (sscanf(line, " %79[^=]=%79s", key, value) != 2) continue;
        if (strcmp(key, "MICROFX_OUTPUT_WIDTH") == 0) config->outputWidth = atoi(value);
        else if (strcmp(key, "MICROFX_OUTPUT_HEIGHT") == 0) config->outputHeight = atoi(value);
        else if (strcmp(key, "MICROFX_TARGET_FPS") == 0) config->targetFps = atoi(value);
        else if (strcmp(key, "MICROFX_MIN_PIXEL_DENSITY") == 0) config->minimumPixelDensity = strtof(value, NULL);
        else if (strcmp(key, "MICROFX_COLOR_FORMAT") == 0) config->colorFormat = strcmp(value, "rgba8888") == 0 ? MICROFX_COLOR_RGBA8888 : MICROFX_COLOR_RGB565;
        else if (strcmp(key, "MICROFX_DEPTH_BITS") == 0) config->depthBits = atoi(value);
        else if (strcmp(key, "MICROFX_DITHERING") == 0) config->dithering = atoi(value) != 0;
        else if (strcmp(key, "MICROFX_PROFILE") == 0) config->profiling = atoi(value) != 0;
        else if (strcmp(key, "MICROFX_PROFILE_INTERVAL") == 0) config->profileIntervalFrames = atoi(value);
        else if (strcmp(key, "MICROFX_DENSITY_SAMPLE_FRAMES") == 0) config->densitySampleFrames = atoi(value);
        else if (strcmp(key, "MICROFX_DENSITY_STEP") == 0) config->densityStep = strtof(value, NULL);
        else if (strcmp(key, "MICROFX_DENSITY_DOWN_THRESHOLD") == 0) config->densityDownThreshold = strtof(value, NULL);
        else if (strcmp(key, "MICROFX_DENSITY_UP_THRESHOLD") == 0) config->densityUpThreshold = strtof(value, NULL);
        else if (strcmp(key, "MICROFX_DENSITY_UP_SAMPLES") == 0) config->densityUpSamples = atoi(value);
        else if (strcmp(key, "MICROFX_ANTIALIASING") == 0) config->antialiasing = strcmp(value, "msaa4") == 0 ? MICROFX_ANTIALIAS_MSAA4 : MICROFX_ANTIALIAS_NONE;
        else if (strcmp(key, "MICROFX_PIXEL_DENSITY") == 0) {
            config->automaticDensity = (strcmp(value, "auto") == 0);
            if (!config->automaticDensity) config->pixelDensity = strtof(value, NULL);
        }
    }
    fclose(file);
}

static void ApplyEnvironment(RuntimeConfig *config)
{
    const char *value = getenv("MICROFX_OUTPUT_WIDTH");
    if (value != NULL) config->outputWidth = atoi(value);
    value = getenv("MICROFX_OUTPUT_HEIGHT");
    if (value != NULL) config->outputHeight = atoi(value);
    value = getenv("MICROFX_TARGET_FPS");
    if (value != NULL) config->targetFps = atoi(value);
    value = getenv("MICROFX_MIN_PIXEL_DENSITY");
    if (value != NULL) config->minimumPixelDensity = strtof(value, NULL);
    value = getenv("MICROFX_PIXEL_DENSITY");
    if (value != NULL) {
        config->automaticDensity = (strcmp(value, "auto") == 0);
        if (!config->automaticDensity) config->pixelDensity = strtof(value, NULL);
    }
    value = getenv("MICROFX_AUTO_DENSITY_VALUE");
    if (value != NULL && config->automaticDensity) config->pixelDensity = strtof(value, NULL);
    value = getenv("MICROFX_COLOR_FORMAT");
    if (value != NULL) config->colorFormat = strcmp(value, "rgba8888") == 0 ? MICROFX_COLOR_RGBA8888 : MICROFX_COLOR_RGB565;
    value = getenv("MICROFX_DEPTH_BITS");
    if (value != NULL) config->depthBits = atoi(value);
    value = getenv("MICROFX_DITHERING");
    if (value != NULL) config->dithering = atoi(value) != 0;
    value = getenv("MICROFX_ANTIALIASING");
    if (value != NULL) config->antialiasing = strcmp(value, "msaa4") == 0 ? MICROFX_ANTIALIAS_MSAA4 : MICROFX_ANTIALIAS_NONE;
    value = getenv("MICROFX_PROFILE");
    if (value != NULL) config->profiling = atoi(value) != 0;
    value = getenv("MICROFX_PROFILE_INTERVAL");
    if (value != NULL) config->profileIntervalFrames = atoi(value);
    value = getenv("MICROFX_DENSITY_SAMPLE_FRAMES");
    if (value != NULL) config->densitySampleFrames = atoi(value);
    value = getenv("MICROFX_DENSITY_STEP");
    if (value != NULL) config->densityStep = strtof(value, NULL);
    value = getenv("MICROFX_DENSITY_DOWN_THRESHOLD");
    if (value != NULL) config->densityDownThreshold = strtof(value, NULL);
    value = getenv("MICROFX_DENSITY_UP_THRESHOLD");
    if (value != NULL) config->densityUpThreshold = strtof(value, NULL);
    value = getenv("MICROFX_DENSITY_UP_SAMPLES");
    if (value != NULL) config->densityUpSamples = atoi(value);
}

static bool ReadDrmNativeMode(int *width, int *height)
{
    const char *modeFiles[] = {
        "/sys/class/drm/card1-HDMI-A-1/modes",
        "/sys/class/drm/card0-HDMI-A-1/modes",
        "/sys/class/drm/card1-HDMI-A-2/modes",
        "/sys/class/drm/card0-HDMI-A-2/modes"
    };
    for (unsigned int i = 0; i < sizeof(modeFiles)/sizeof(modeFiles[0]); i++) {
        FILE *file = fopen(modeFiles[i], "r");
        if (file == NULL) continue;
        int candidateWidth = 0;
        int candidateHeight = 0;
        int matched = fscanf(file, "%dx%d", &candidateWidth, &candidateHeight);
        fclose(file);
        if (matched == 2 && candidateWidth >= 320 && candidateHeight >= 240) {
            *width = candidateWidth;
            *height = candidateHeight;
            return true;
        }
    }
    return false;
}

static bool ReadClosestDrmMode(int targetWidth, int targetHeight,
                               int *selectedWidth, int *selectedHeight)
{
    const char *modeFiles[] = {
        "/sys/class/drm/card1-HDMI-A-1/modes",
        "/sys/class/drm/card0-HDMI-A-1/modes",
        "/sys/class/drm/card1-HDMI-A-2/modes",
        "/sys/class/drm/card0-HDMI-A-2/modes"
    };
    float bestScore = 1.0e9f;
    bool found = false;
    for (unsigned int fileIndex = 0;
         fileIndex < sizeof(modeFiles)/sizeof(modeFiles[0]); fileIndex++) {
        FILE *file = fopen(modeFiles[fileIndex], "r");
        if (file == NULL) continue;
        char line[80];
        while (fgets(line, sizeof(line), file) != NULL) {
            int width = 0;
            int height = 0;
            if (sscanf(line, "%dx%d", &width, &height) != 2) continue;
            if (width < 320 || height < 240) continue;
            float targetArea = (float)targetWidth*targetHeight;
            float candidateArea = (float)width*height;
            float targetAspect = (float)targetWidth/targetHeight;
            float candidateAspect = (float)width/height;
            float score = fabsf(logf(candidateArea/targetArea));
            score += fabsf(candidateAspect - targetAspect)*4.0f;
            if (candidateArea > targetArea) score += 0.025f;
            if (score < bestScore) {
                bestScore = score;
                *selectedWidth = width;
                *selectedHeight = height;
                found = true;
            }
        }
        fclose(file);
        if (found) break;
    }
    return found;
}

static RuntimeConfig LoadRuntimeConfig(const MicroFxScene *scene)
{
    RuntimeConfig config = {
        .outputWidth = 0,
        .outputHeight = 0,
        .nativeWidth = 0,
        .nativeHeight = 0,
        .targetFps = 30,
        .pixelDensity = 1.0f,
        .minimumPixelDensity = 0.5f,
        .colorFormat = MICROFX_COLOR_RGB565,
        .antialiasing = MICROFX_ANTIALIAS_NONE,
        .depthBits = 16,
        .automaticDensity = true,
        .dithering = true,
        .resolutionFixed = false,
        .profiling = false,
        .profileIntervalFrames = 120,
        .densitySampleFrames = 60,
        .densityStep = 0.1f,
        .densityDownThreshold = 1.08f,
        .densityUpThreshold = 0.72f,
        .densityUpSamples = 4
    };
    ApplyConfigFile(&config, "/etc/microfx.conf");
    ApplyConfigFile(&config, "/data/config/microfx.conf");
    if (scene->runtime.configured) {
        config.outputWidth = scene->runtime.outputWidth;
        config.outputHeight = scene->runtime.outputHeight;
        config.targetFps = scene->runtime.targetFps;
        config.pixelDensity = scene->runtime.pixelDensity;
        config.minimumPixelDensity = scene->runtime.minimumPixelDensity;
        config.automaticDensity = scene->runtime.automaticDensity;
        config.colorFormat = scene->runtime.colorFormat;
        config.antialiasing = scene->runtime.antialiasing;
        config.depthBits = scene->runtime.depthBits;
        config.dithering = scene->runtime.dithering;
        config.profiling = scene->runtime.profiling;
        config.profileIntervalFrames = scene->runtime.profileIntervalFrames;
        config.densitySampleFrames = scene->runtime.densitySampleFrames;
        config.densityStep = scene->runtime.densityStep;
        config.densityDownThreshold = scene->runtime.densityDownThreshold;
        config.densityUpThreshold = scene->runtime.densityUpThreshold;
        config.densityUpSamples = scene->runtime.densityUpSamples;
    }
    ApplyEnvironment(&config);

    config.resolutionFixed = (config.outputWidth > 0 && config.outputHeight > 0);
    if (!config.resolutionFixed) {
        if (!ReadDrmNativeMode(&config.nativeWidth, &config.nativeHeight)) {
            config.nativeWidth = DEFAULT_OUTPUT_WIDTH;
            config.nativeHeight = DEFAULT_OUTPUT_HEIGHT;
        }
        config.outputWidth = config.nativeWidth;
        config.outputHeight = config.nativeHeight;
    } else {
        config.nativeWidth = config.outputWidth;
        config.nativeHeight = config.outputHeight;
    }
    config.targetFps = (config.targetFps < 1) ? 30 : config.targetFps;
    config.profileIntervalFrames = config.profileIntervalFrames < 30 ? 120 : config.profileIntervalFrames;
    config.densitySampleFrames = config.densitySampleFrames < 30 ? 60 : config.densitySampleFrames;
    config.densityStep = Clamp(config.densityStep, 0.01f, 0.25f);
    config.densityDownThreshold = Clamp(config.densityDownThreshold, 1.0f, 2.0f);
    config.densityUpThreshold = Clamp(config.densityUpThreshold, 0.25f,
                                      config.densityDownThreshold - 0.01f);
    config.densityUpSamples = config.densityUpSamples < 1 ? 4 : config.densityUpSamples;
    config.minimumPixelDensity = Clamp(config.minimumPixelDensity, 0.25f, 1.0f);
    config.pixelDensity = Clamp(config.pixelDensity, config.minimumPixelDensity, 1.0f);
    if (config.resolutionFixed) {
        config.automaticDensity = false;
        config.pixelDensity = 1.0f;
    } else if (config.pixelDensity < 0.999f) {
        int targetWidth = (int)roundf(config.nativeWidth*config.pixelDensity);
        int targetHeight = (int)roundf(config.nativeHeight*config.pixelDensity);
        int selectedWidth = targetWidth;
        int selectedHeight = targetHeight;
        if (ReadClosestDrmMode(targetWidth, targetHeight,
                               &selectedWidth, &selectedHeight)) {
            config.outputWidth = selectedWidth;
            config.outputHeight = selectedHeight;
        } else {
            config.outputWidth = targetWidth;
            config.outputHeight = targetHeight;
        }
    }
    return config;
}

static bool FindAdjacentDrmMode(const RuntimeConfig *config, int direction,
                                int currentWidth, int currentHeight,
                                float *selectedDensity,
                                int *selectedWidth, int *selectedHeight)
{
    float candidate = config->pixelDensity;
    while ((direction < 0 && candidate > config->minimumPixelDensity + 0.001f) ||
           (direction > 0 && candidate < 0.999f)) {
        candidate = direction < 0
                  ? fmaxf(candidate - config->densityStep,
                          config->minimumPixelDensity)
                  : fminf(candidate + config->densityStep, 1.0f);
        int targetWidth = (int)roundf(config->nativeWidth*candidate);
        int targetHeight = (int)roundf(config->nativeHeight*candidate);
        int width = targetWidth;
        int height = targetHeight;
        ReadClosestDrmMode(targetWidth, targetHeight, &width, &height);

        // Density is an ergonomic control, but DRM exposes a discrete mode
        // list. Skip density values that resolve to the mode already in use.
        if (width != currentWidth || height != currentHeight) {
            *selectedDensity = candidate;
            *selectedWidth = width;
            *selectedHeight = height;
            return true;
        }
        if ((direction < 0 && candidate <= config->minimumPixelDensity + 0.001f) ||
            (direction > 0 && candidate >= 0.999f)) break;
    }
    return false;
}


static double ProcessCpuMilliseconds(void)
{
    struct timespec value = { 0 };
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0) return 0.0;
    return value.tv_sec*1000.0 + value.tv_nsec/1000000.0;
}

typedef struct {
    double script, begin, background, mesh, overlay, interface, present;
    double overlayQuads, sdf, image, text;
    double wall, processCpu, nonCpu, maxWall;
    unsigned int samples, overBudget;
} FrameProfile;

static void AddProfileSample(FrameProfile *profile,
                             double frameStart, double scriptEnd,
                             double beginEnd, double backgroundEnd,
                             double meshEnd, double overlayQuadsEnd,
                             double sdfEnd, double imageEnd, double overlayEnd,
                             double interfaceEnd, double presentEnd,
                             double processCpuMs, double frameBudgetMs)
{
    profile->script += (scriptEnd - frameStart)*1000.0;
    profile->begin += (beginEnd - scriptEnd)*1000.0;
    profile->background += (backgroundEnd - beginEnd)*1000.0;
    profile->mesh += (meshEnd - backgroundEnd)*1000.0;
    profile->overlayQuads += (overlayQuadsEnd - meshEnd)*1000.0;
    profile->sdf += (sdfEnd - overlayQuadsEnd)*1000.0;
    profile->image += (imageEnd - sdfEnd)*1000.0;
    profile->text += (overlayEnd - imageEnd)*1000.0;
    profile->overlay += (overlayEnd - meshEnd)*1000.0;
    profile->interface += (interfaceEnd - overlayEnd)*1000.0;
    profile->present += (presentEnd - interfaceEnd)*1000.0;
    const double wallMs = (presentEnd - frameStart)*1000.0;
    profile->wall += wallMs;
    profile->processCpu += processCpuMs;
    profile->nonCpu += fmax(wallMs - processCpuMs, 0.0);
    if (wallMs > profile->maxWall) profile->maxWall = wallMs;
    if (wallMs > frameBudgetMs) profile->overBudget++;
    profile->samples++;
}

static void ReportAndResetProfile(FrameProfile *profile,
                                  int outputWidth, int outputHeight,
                                  float density, int fps, int targetFps)
{
    if (profile->samples == 0) return;
    const double divisor = profile->samples;
    const double budget = 1000.0/targetFps;
    printf("MICROFX_PROFILE frames=%u output=%dx%d density=%.3f fps=%d target_fps=%d budget=%.3f "
           "script=%.3f begin=%.3f background=%.3f mesh=%.3f "
           "overlay=%.3f overlay_quads=%.3f sdf=%.3f image=%.3f text=%.3f "
           "interface=%.3f present=%.3f cpu=%.3f noncpu=%.3f "
           "wall=%.3f max_wall=%.3f over_budget=%u\n",
           profile->samples, outputWidth, outputHeight, density, fps, targetFps, budget,
           profile->script/divisor, profile->begin/divisor,
           profile->background/divisor, profile->mesh/divisor,
           profile->overlay/divisor, profile->overlayQuads/divisor,
           profile->sdf/divisor, profile->image/divisor, profile->text/divisor,
           profile->interface/divisor,
           profile->present/divisor, profile->processCpu/divisor,
           profile->nonCpu/divisor, profile->wall/divisor, profile->maxWall,
           profile->overBudget);
    fflush(stdout);
    memset(profile, 0, sizeof(*profile));
}

static void DrawInterface(int fps, float time,
                          float cpuAverageMs, float gpuAverageMs,
                          bool automaticDensity, float pixelDensity,
                          int outputWidth, int outputHeight)
{
    const int barX = 38;
    const int barY = DESIGN_HEIGHT - 54;
    const int barHeight = 34;
    const int padding = 10;
    const int gap = 12;
    const int fontSize = 16;
    const int meterWidth = 21;
    const int meterHeight = fontSize;
    const int meterY = barY + (barHeight - meterHeight)/2;
    const float meterScale = (float)meterWidth/33.333f;
    int cpuWidth = (int)fminf(cpuAverageMs*meterScale, (float)meterWidth);
    int gpuWidth = (int)fminf(gpuAverageMs*meterScale, (float)meterWidth);
    const float minute=60.0f,hour=3600.0f,day=86400.0f,year=31557600.0f;
    const char *unit="s";float uptime=time;
    if(time>=year){uptime=time/year;unit="y";}
    else if(time>=day){uptime=time/day;unit="d";}
    else if(time>=hour){uptime=time/hour;unit="h";}
    else if(time>=minute){uptime=time/minute;unit="m";}
    char up[32],fpsText[24],mode[24],resolution[32];
    snprintf(up,sizeof(up),"UP %.1f%s",uptime,unit);
    snprintf(fpsText,sizeof(fpsText),"%d FPS",fps);
    if (GetScreenWidth() == outputWidth && GetScreenHeight() == outputHeight) {
        snprintf(resolution,sizeof(resolution),"%dx%d",outputWidth,outputHeight);
    } else {
        snprintf(resolution,sizeof(resolution),"%dx%d>%dx%d",
                 GetScreenWidth(),GetScreenHeight(),outputWidth,outputHeight);
    }
    snprintf(mode,sizeof(mode),"%s %.2f",automaticDensity?"AUTO":"FIXED",
             pixelDensity);
    int width=padding*2+MeasureText(up,fontSize)+MeasureText(fpsText,fontSize)+
      MeasureText(resolution,fontSize)+MeasureText(mode,fontSize)+
      MeasureText("CPU",fontSize)+MeasureText("GPU",fontSize)+meterWidth*2+gap*7;
    DrawRectangle(barX,barY,width,barHeight,(Color){4,8,22,245});
    DrawRectangleLines(barX,barY,width,barHeight,(Color){65,105,145,255});
    int x=barX+padding,y=barY+(barHeight-fontSize)/2;
#define DRAW_FIELD(value,red,green,blue) do { \
    DrawText((value),x,y,fontSize,(Color){red,green,blue,255}); \
    x+=MeasureText((value),fontSize)+gap; \
} while(0)
    DRAW_FIELD(up,180,205,235);
    DRAW_FIELD(fpsText,255,210,70);
    DRAW_FIELD(resolution,245,245,245);
    DRAW_FIELD(mode,180,205,235);
    DRAW_FIELD("CPU",40,205,255);
    DrawRectangle(x,meterY,meterWidth,meterHeight,(Color){35,42,66,255});
    DrawRectangle(x,meterY,cpuWidth,meterHeight,(Color){40,205,255,255});x+=meterWidth+gap;
    DRAW_FIELD("GPU",255,85,180);
    DrawRectangle(x,meterY,meterWidth,meterHeight,(Color){35,42,66,255});
    DrawRectangle(x,meterY,gpuWidth,meterHeight,(Color){255,85,180,255});
#undef DRAW_FIELD
}

int main(void)
{
    if (chdir("/tmp") != 0) fprintf(stderr, "Could not switch screenshot workspace to /tmp\n");
    MicroFxScene scriptScene;
    MicroFxSceneInit(&scriptScene);
    const char *scriptOverride = getenv("MICROFX_SCRIPT");
    const char *scriptPath = (scriptOverride != NULL && scriptOverride[0] != '\0')
                           ? scriptOverride
                           : (access("/data/apps/current/main.js", R_OK) == 0)
                           ? "/data/apps/current/main.js"
                           : "/usr/share/microfx/main.js";
    MicroFxScript *script = MicroFxScriptCreate(&scriptScene, scriptPath);
    if (script == NULL) exit(EXIT_FAILURE);
    RuntimeConfig runtime = LoadRuntimeConfig(&scriptScene);
    // The DRM backend already waits for the page-flip completion event. Asking
    // EGL for VSync as well can serialize two independent synchronization
    // points on etnaviv and quantize the application to a lower divisor of 60.
    setenv("MICROFX_COLOR_FORMAT",
           runtime.colorFormat == MICROFX_COLOR_RGBA8888
               ? "rgba8888" : "rgb565", 1);
    char depthValue[8];
    snprintf(depthValue, sizeof(depthValue), "%d", runtime.depthBits);
    setenv("MICROFX_DEPTH_BITS", depthValue, 1);
    setenv("MICROFX_PROFILE", runtime.profiling ? "1" : "0", 1);
    unsigned int windowFlags = FLAG_FULLSCREEN_MODE;
    if (runtime.antialiasing == MICROFX_ANTIALIAS_MSAA4) windowFlags |= FLAG_MSAA_4X_HINT;
    SetConfigFlags(windowFlags);
    InitWindow(runtime.outputWidth, runtime.outputHeight, MICROFX_PRODUCT_NAME " DEMO");
    SetTargetFPS(0);
    if (runtime.dithering) glEnable(GL_DITHER); else glDisable(GL_DITHER);

    EGLBoolean immediateSwap = eglSwapInterval(eglGetCurrentDisplay(), 0);
    printf("CANVAS_EGL swap_interval=0 result=%d error=0x%x\n",
           immediateSwap, eglGetError());

    GLint depthBits = 0;
    GLint samples = 0;
    glGetIntegerv(GL_DEPTH_BITS, &depthBits);
    glGetIntegerv(GL_SAMPLES, &samples);
    printf("CANVAS_GL depth_bits=%d samples=%d refresh=%d renderer=%s version=%s\n",
           depthBits, samples, GetMonitorRefreshRate(GetCurrentMonitor()),
           glGetString(GL_RENDERER), glGetString(GL_VERSION));
    printf("MICROFX_DISPLAY output=%dx%d target_fps=%d density=%s%.2f minimum=%.2f\n",
           GetScreenWidth(), GetScreenHeight(), runtime.targetFps,
           runtime.automaticDensity ? "auto:" : "fixed:", runtime.pixelDensity,
           runtime.minimumPixelDensity);
    printf("MICROFX_QUALITY color=%s depth=%d dithering=%d antialiasing=%s\n",
           runtime.colorFormat == MICROFX_COLOR_RGBA8888 ? "rgba8888" : "rgb565",
           runtime.depthBits, runtime.dithering,
           runtime.antialiasing == MICROFX_ANTIALIAS_MSAA4 ? "msaa4" : "none");
    fflush(stdout);

    MicroFxSdfRenderer sdfRenderer;
    if (!MicroFxSdfRendererInit(&sdfRenderer)) {
        fprintf(stderr, "MICROFX_SDF fatal initialization failure\n");
        exit(EXIT_FAILURE);
    }
    MicroFxQuadRenderer quadRenderer;
    if (!MicroFxQuadRendererInit(&quadRenderer)) {
        fprintf(stderr, "MICROFX_QUAD fatal initialization failure\n");
        exit(EXIT_FAILURE);
    }
    MicroFxMeshRenderer meshRenderer;
    if (!MicroFxMeshRendererInit(&meshRenderer)) {
        fprintf(stderr, "MICROFX_MESH fatal initialization failure\n");
        exit(EXIT_FAILURE);
    }
    MicroFxTextRenderer textRenderer;
    if (!MicroFxTextRendererInit(&textRenderer, GetFontDefault())) {
        fprintf(stderr, "MICROFX_TEXT fatal initialization failure\n");
        exit(EXIT_FAILURE);
    }
    MicroFxImageRenderer imageRenderer;
    if (!MicroFxImageRendererInit(&imageRenderer)) {
        fprintf(stderr, "MICROFX_IMAGE fatal initialization failure\n");
        exit(EXIT_FAILURE);
    }
    int renderedFrames = 0;
    float cpuAverageMs = 0.0f;
    float gpuAverageMs = 0.0f;
    bool restartAtChangedResolution = false;
    float requestedRestartDensity = runtime.pixelDensity;
    int densityUnderBudgetSamples = 0;
    double previousScriptTime = GetTime();
    FrameProfile profile = { 0 };
    const bool synchronizedProfiling = runtime.profiling &&
        access("/run/microfx-profile-sync", F_OK) == 0;
    if (synchronizedProfiling) {
        printf("MICROFX_PROFILE_SYNC enabled\n");
        fflush(stdout);
    }
    while (!WindowShouldClose()) {
        double frameStart = GetTime();
        double processCpuStart = ProcessCpuMilliseconds();
        float time = (float)GetTime();
        double scriptDelta = fmin(fmax((double)time - previousScriptTime, 0.0), 0.25);
        previousScriptTime = time;
        if (!MicroFxScriptUpdate(script, time, scriptDelta)) {
            fprintf(stderr, "MICROFX_JS fail-fast update error\n");
            break;
        }
        double scriptEnd = GetTime();

        BeginDrawing();
        if (!MicroFxSceneHasOpaqueCoveringBackground(&scriptScene)) {
            ClearBackground((Color){ 8, 12, 29, 255 });
            rlDrawRenderBatchActive();
        }
        if (synchronizedProfiling) glFinish();
        double beginEnd = GetTime();
        MicroFxQuadRendererDraw(&quadRenderer,&scriptScene,
                               DESIGN_WIDTH,DESIGN_HEIGHT,true);
        if (synchronizedProfiling) glFinish();
        double backgroundEnd = GetTime();
        Matrix scriptView = { 0 };
        Matrix scriptProjection = { 0 };
        Camera3D scriptCamera = { 0 };
        scriptCamera.position = (Vector3){ scriptScene.camera.position[0],
                                           scriptScene.camera.position[1],
                                           scriptScene.camera.position[2] };
        scriptCamera.target = (Vector3){ scriptScene.camera.target[0],
                                         scriptScene.camera.target[1],
                                         scriptScene.camera.target[2] };
        scriptCamera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        scriptCamera.fovy = scriptScene.camera.fovY;
        scriptCamera.projection = CAMERA_PERSPECTIVE;
        // The preceding 2D background pass disables depth writes. GLES depth
        // clears obey that mask, so restore it before clearing the new frame.
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        scriptView = MatrixLookAt(scriptCamera.position, scriptCamera.target,
                                  scriptCamera.up);
        scriptProjection = MatrixPerspective(scriptCamera.fovy*DEG2RAD,
                                              (double)GetScreenWidth()/GetScreenHeight(),
                                              0.05, 100.0);
        float16 scriptViewValues = MatrixToFloatV(scriptView);
        float16 scriptProjectionValues = MatrixToFloatV(scriptProjection);
        if (!MicroFxMeshRendererDraw(&meshRenderer, &scriptScene,
                                    scriptViewValues.v, scriptProjectionValues.v)) {
            fprintf(stderr, "MICROFX_MESH fatal asset or renderer failure\n");
            break;
        }
        if (synchronizedProfiling) glFinish();
        double meshEnd = GetTime();
        // Retained 2D and text are overlays and therefore follow every 3D pass.
        MicroFxQuadRendererDraw(&quadRenderer, &scriptScene,
                               DESIGN_WIDTH, DESIGN_HEIGHT, false);
        if (synchronizedProfiling) glFinish();
        double overlayQuadsEnd = GetTime();
        MicroFxSdfRendererDraw(&sdfRenderer, &scriptScene,
                              DESIGN_WIDTH, DESIGN_HEIGHT);
        if (synchronizedProfiling) glFinish();
        double sdfEnd = GetTime();
        if (!MicroFxImageRendererDraw(&imageRenderer, &scriptScene,
                                      DESIGN_WIDTH, DESIGN_HEIGHT)) {
            fprintf(stderr, "MICROFX_IMAGE fatal asset or renderer failure\n");
            break;
        }
        if (synchronizedProfiling) glFinish();
        double imageEnd = GetTime();
        if (!MicroFxTextRendererDraw(&textRenderer, &scriptScene,
                                     DESIGN_WIDTH, DESIGN_HEIGHT)) {
            fprintf(stderr, "MICROFX_TEXT fatal asset or renderer failure\n");
            break;
        }
        if (synchronizedProfiling) glFinish();
        double overlayEnd = GetTime();

        if (scriptScene.runtime.debugBarUntilSeconds < 0.0f ||
            time < scriptScene.runtime.debugBarUntilSeconds) {
            rlPushMatrix();
            rlScalef((float)GetScreenWidth()/DESIGN_WIDTH,
                     (float)GetScreenHeight()/DESIGN_HEIGHT, 1.0f);
            DrawInterface(GetFPS(), time, cpuAverageMs, gpuAverageMs,
                          runtime.automaticDensity, runtime.pixelDensity,
                          runtime.outputWidth, runtime.outputHeight);
            rlPopMatrix();
        }
        if (synchronizedProfiling) glFinish();
        double interfaceEnd = GetTime();
        EndDrawing();
        double presentEnd = GetTime();
        double processCpuEnd = ProcessCpuMilliseconds();
        float frameCpuMs = (float)fmax(processCpuEnd - processCpuStart, 0.0);
        float frameWallMs = (float)fmax((presentEnd - frameStart)*1000.0, 0.0);
        float frameGpuMs = fmaxf(frameWallMs - frameCpuMs, 0.0f);
        const float averageWeight = (renderedFrames < 30) ? 0.18f : 0.04f;
        if (renderedFrames == 0) {
            cpuAverageMs = frameCpuMs;
            gpuAverageMs = frameGpuMs;
        } else {
            cpuAverageMs += (frameCpuMs - cpuAverageMs)*averageWeight;
            gpuAverageMs += (frameGpuMs - gpuAverageMs)*averageWeight;
        }
        if (runtime.profiling) {
            AddProfileSample(&profile, frameStart, scriptEnd, beginEnd,
                             backgroundEnd, meshEnd, overlayQuadsEnd, sdfEnd,
                             imageEnd, overlayEnd, interfaceEnd,
                             presentEnd, frameCpuMs, 1000.0/runtime.targetFps);
        }

        if (FileExists("/tmp/canvas-capture.request")) {
            remove("/tmp/canvas-capture.request");
            remove("/tmp/canvas-screen.png");
            remove("/tmp/canvas-screen.tmp.png");
            TakeScreenshot("/tmp/canvas-screen.tmp.png");
            if (rename("/tmp/canvas-screen.tmp.png", "/tmp/canvas-screen.png") == 0) {
                printf("CANVAS_CAPTURE /tmp/canvas-screen.png\n");
                fflush(stdout);
            }
        }

        renderedFrames++;
        if (scriptScene.runtime.durationSeconds > 0.0f &&
            time >= scriptScene.runtime.durationSeconds) {
            printf("MICROFX_DURATION complete seconds=%.1f\n",
                   scriptScene.runtime.durationSeconds);
            fflush(stdout);
            break;
        }
        if (runtime.automaticDensity &&
            (renderedFrames % runtime.densitySampleFrames) == 0) {
            const float frameBudgetMs = 1000.0f/runtime.targetFps;
            const float averageFrameMs = cpuAverageMs + gpuAverageMs;
            const MicroFxDensityPolicy densityPolicy = {
                runtime.densityDownThreshold,
                runtime.densityUpThreshold,
                runtime.densityUpSamples
            };
            MicroFxDensityDecision decision = MicroFxEvaluateDensity(
                &densityPolicy, averageFrameMs, frameBudgetMs,
                runtime.pixelDensity, runtime.minimumPixelDensity,
                &densityUnderBudgetSamples);
            if (decision != MICROFX_DENSITY_KEEP) {
                int nextWidth = GetScreenWidth();
                int nextHeight = GetScreenHeight();
                if (FindAdjacentDrmMode(&runtime, (int)decision,
                                        GetScreenWidth(), GetScreenHeight(),
                                        &requestedRestartDensity,
                                        &nextWidth, &nextHeight)) {
                    restartAtChangedResolution = true;
                    printf("MICROFX_RESOLUTION restart direction=%s density=%.3f mode=%dx%d target_fps=%d average_ms=%.2f\n",
                           decision == MICROFX_DENSITY_LOWER ? "lower" : "raise",
                           requestedRestartDensity, nextWidth, nextHeight,
                           runtime.targetFps, averageFrameMs);
                    fflush(stdout);
                    break;
                }
                printf("MICROFX_RESOLUTION %s limit reached at %dx%d average_ms=%.2f\n",
                       decision == MICROFX_DENSITY_LOWER ? "lower" : "upper",
                       GetScreenWidth(), GetScreenHeight(), averageFrameMs);
                fflush(stdout);
            }
        }
        if (runtime.profiling &&
            (renderedFrames % runtime.profileIntervalFrames) == 0) {
            ReportAndResetProfile(&profile, runtime.outputWidth, runtime.outputHeight,
                                  runtime.pixelDensity, GetFPS(), runtime.targetFps);
        }
    }

    MicroFxScriptDestroy(script);
    MicroFxSdfRendererDestroy(&sdfRenderer);
    MicroFxQuadRendererDestroy(&quadRenderer);
    MicroFxMeshRendererDestroy(&meshRenderer);
    MicroFxImageRendererDestroy(&imageRenderer);
    MicroFxTextRendererDestroy(&textRenderer);
    CloseWindow();
    if (restartAtChangedResolution) {
        char densityValue[24];
        snprintf(densityValue, sizeof(densityValue), "%.3f", requestedRestartDensity);
        setenv("MICROFX_AUTO_DENSITY_VALUE", densityValue, 1);
        execl("/proc/self/exe", "canvas-demo", (char *)NULL);
        perror("MICROFX_DENSITY exec");
        return EXIT_FAILURE;
    }
    return 0;
}
