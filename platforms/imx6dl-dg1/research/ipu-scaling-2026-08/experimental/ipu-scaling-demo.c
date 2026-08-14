#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

typedef struct {
    drmModeConnector *connector;
    drmModeModeInfo mode;
    uint32_t crtcId;
    int crtcIndex;
    uint32_t primaryPlaneId;
    uint32_t overlayPlaneId;
} DisplayPath;

typedef struct {
    struct gbm_device *device;
    struct gbm_surface *surface;
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface eglSurface;
} GraphicsContext;

typedef struct {
    struct gbm_bo *bo;
    uint32_t fbId;
} SceneFrame;

typedef struct {
    struct drm_mode_create_dumb create;
    void *mapping;
    uint32_t fbId;
} DumbOverlay;

typedef struct {
    uint32_t planeId;
    uint32_t fbId;
    uint32_t sourceWidth;
    uint32_t sourceHeight;
    int32_t destinationX;
    int32_t destinationY;
    uint32_t destinationWidth;
    uint32_t destinationHeight;
    uint64_t zPosition;
} PlaneLayout;

typedef struct {
    struct gbm_bo *bo;
    int dmaBufFd;
    uint32_t fbId;
} NativeFrame;

typedef struct {
    int fd;
    bool streaming;
    uint32_t inputSize;
    uint32_t outputSize;
    uint32_t inputSlots;
    uint32_t outputSlots;
} IpuConverter;

static volatile sig_atomic_t stopRequested;

static void HandleSignal(int signalNumber)
{
    (void)signalNumber;
    stopRequested = 1;
}

static double MonotonicMilliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

static double ProcessCpuMilliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

static int IoctlRetry(int fd, unsigned long request, void *argument)
{
    int result;
    do result = ioctl(fd, request, argument); while (result < 0 && errno == EINTR);
    return result;
}

static uint32_t FindPropertyId(int fd, uint32_t objectId,
                               uint32_t objectType, const char *name)
{
    drmModeObjectProperties *properties = drmModeObjectGetProperties(fd, objectId, objectType);
    if (!properties) return 0;
    uint32_t result = 0;
    for (uint32_t index = 0; index < properties->count_props; index++) {
        drmModePropertyRes *property = drmModeGetProperty(fd, properties->props[index]);
        if (!property) continue;
        if (strcmp(property->name, name) == 0) result = property->prop_id;
        drmModeFreeProperty(property);
        if (result) break;
    }
    drmModeFreeObjectProperties(properties);
    return result;
}

static bool ReadPlaneType(int fd, uint32_t planeId, uint64_t *type)
{
    drmModeObjectProperties *properties = drmModeObjectGetProperties(
        fd, planeId, DRM_MODE_OBJECT_PLANE);
    if (!properties) return false;
    bool found = false;
    for (uint32_t index = 0; index < properties->count_props; index++) {
        drmModePropertyRes *property = drmModeGetProperty(fd, properties->props[index]);
        if (!property) continue;
        if (strcmp(property->name, "type") == 0) {
            *type = properties->prop_values[index];
            found = true;
        }
        drmModeFreeProperty(property);
        if (found) break;
    }
    drmModeFreeObjectProperties(properties);
    return found;
}

static int AddAtomicProperty(int fd, drmModeAtomicReq *request,
                             uint32_t objectId, uint32_t objectType,
                             const char *name, uint64_t value, bool required)
{
    uint32_t propertyId = FindPropertyId(fd, objectId, objectType, name);
    if (!propertyId) {
        if (required) fprintf(stderr, "ERROR missing-property object=%u name=%s\n", objectId, name);
        return required ? -1 : 0;
    }
    if (drmModeAtomicAddProperty(request, objectId, propertyId, value) < 0) {
        fprintf(stderr, "ERROR add-property object=%u name=%s errno=%d message=%s\n",
                objectId, name, errno, strerror(errno));
        return -1;
    }
    return 0;
}

static bool BetterFallbackMode(const drmModeModeInfo *candidate,
                               const drmModeModeInfo *best)
{
    if (candidate->flags & DRM_MODE_FLAG_INTERLACE) return false;
    uint64_t candidateArea = (uint64_t)candidate->hdisplay * candidate->vdisplay;
    uint64_t bestArea = best ? (uint64_t)best->hdisplay * best->vdisplay : 0;
    if (!best || candidateArea > bestArea) return true;
    if (candidateArea < bestArea) return false;
    if (candidate->vrefresh > best->vrefresh) return true;
    return candidate->vrefresh == best->vrefresh && candidate->clock < best->clock;
}

static int FindDisplayPath(int fd, DisplayPath *path)
{
    memset(path, 0, sizeof(*path));
    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources) return -1;

    for (int index = 0; index < resources->count_connectors; index++) {
        drmModeConnector *connector = drmModeGetConnector(fd, resources->connectors[index]);
        if (!connector) continue;
        if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
            path->connector = connector;
            break;
        }
        drmModeFreeConnector(connector);
    }
    if (!path->connector) {
        fprintf(stderr, "ERROR no-connected-connector\n");
        drmModeFreeResources(resources);
        return -1;
    }

    const drmModeModeInfo *selected = NULL;
    for (int index = 0; index < path->connector->count_modes; index++) {
        const drmModeModeInfo *candidate = &path->connector->modes[index];
        if ((candidate->type & DRM_MODE_TYPE_PREFERRED) &&
            !(candidate->flags & DRM_MODE_FLAG_INTERLACE)) {
            if (BetterFallbackMode(candidate, selected)) selected = candidate;
        }
    }
    if (!selected) {
        for (int index = 0; index < path->connector->count_modes; index++) {
            const drmModeModeInfo *candidate = &path->connector->modes[index];
            if (BetterFallbackMode(candidate, selected)) selected = candidate;
        }
    }
    if (!selected) {
        fprintf(stderr, "ERROR no-progressive-mode\n");
        drmModeFreeResources(resources);
        return -1;
    }
    path->mode = *selected;

    drmModeEncoder *encoder = NULL;
    if (path->connector->encoder_id) encoder = drmModeGetEncoder(fd, path->connector->encoder_id);
    if (!encoder && path->connector->count_encoders > 0) {
        encoder = drmModeGetEncoder(fd, path->connector->encoders[0]);
    }
    if (!encoder || !encoder->crtc_id) {
        fprintf(stderr, "ERROR no-encoder-crtc\n");
        if (encoder) drmModeFreeEncoder(encoder);
        drmModeFreeResources(resources);
        return -1;
    }
    path->crtcId = encoder->crtc_id;
    drmModeFreeEncoder(encoder);

    path->crtcIndex = -1;
    for (int index = 0; index < resources->count_crtcs; index++) {
        if (resources->crtcs[index] == path->crtcId) path->crtcIndex = index;
    }
    if (path->crtcIndex < 0) {
        fprintf(stderr, "ERROR crtc-not-in-resources id=%u\n", path->crtcId);
        drmModeFreeResources(resources);
        return -1;
    }

    drmModePlaneRes *planes = drmModeGetPlaneResources(fd);
    if (!planes) {
        drmModeFreeResources(resources);
        return -1;
    }
    for (uint32_t index = 0; index < planes->count_planes; index++) {
        drmModePlane *plane = drmModeGetPlane(fd, planes->planes[index]);
        if (!plane) continue;
        uint64_t type = UINT64_MAX;
        bool compatible = plane->possible_crtcs & (1u << path->crtcIndex);
        if (compatible && ReadPlaneType(fd, plane->plane_id, &type)) {
            if (type == DRM_PLANE_TYPE_PRIMARY && !path->primaryPlaneId) {
                path->primaryPlaneId = plane->plane_id;
            } else if (type == DRM_PLANE_TYPE_OVERLAY && !path->overlayPlaneId) {
                path->overlayPlaneId = plane->plane_id;
            }
        }
        drmModeFreePlane(plane);
    }
    drmModeFreePlaneResources(planes);
    drmModeFreeResources(resources);
    if (!path->primaryPlaneId || !path->overlayPlaneId) {
        fprintf(stderr, "ERROR required-planes primary=%u overlay=%u\n",
                path->primaryPlaneId, path->overlayPlaneId);
        return -1;
    }
    return 0;
}

static int InitializeGraphics(int fd, int width, int height, GraphicsContext *graphics)
{
    memset(graphics, 0, sizeof(*graphics));
    graphics->display = EGL_NO_DISPLAY;
    graphics->context = EGL_NO_CONTEXT;
    graphics->eglSurface = EGL_NO_SURFACE;
    graphics->device = gbm_create_device(fd);
    if (!graphics->device) {
        fprintf(stderr, "ERROR gbm-device\n");
        return -1;
    }
    graphics->surface = gbm_surface_create(
        graphics->device, (uint32_t)width, (uint32_t)height,
        GBM_FORMAT_XRGB8888,
        GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    if (!graphics->surface) {
        fprintf(stderr, "ERROR gbm-surface errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }

    graphics->display = eglGetDisplay((EGLNativeDisplayType)graphics->device);
    if (graphics->display == EGL_NO_DISPLAY ||
        !eglInitialize(graphics->display, NULL, NULL) ||
        !eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "ERROR egl-initialize code=0x%x\n", eglGetError());
        return -1;
    }
    EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE
    };
    EGLint count = 0;
    if (!eglChooseConfig(graphics->display, attributes, NULL, 0, &count) || count <= 0) {
        fprintf(stderr, "ERROR egl-config-count code=0x%x\n", eglGetError());
        return -1;
    }
    EGLConfig *configs = calloc((size_t)count, sizeof(*configs));
    if (!configs) return -1;
    EGLint returned = 0;
    if (!eglChooseConfig(graphics->display, attributes, configs, count, &returned)) {
        free(configs);
        return -1;
    }
    for (EGLint index = 0; index < returned; index++) {
        EGLint visual = 0;
        if (eglGetConfigAttrib(graphics->display, configs[index], EGL_NATIVE_VISUAL_ID, &visual) &&
            (uint32_t)visual == GBM_FORMAT_XRGB8888) {
            graphics->config = configs[index];
            break;
        }
    }
    free(configs);
    if (!graphics->config) {
        fprintf(stderr, "ERROR no-xrgb8888-egl-config\n");
        return -1;
    }

    EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    graphics->context = eglCreateContext(
        graphics->display, graphics->config, EGL_NO_CONTEXT, contextAttributes);
    graphics->eglSurface = eglCreateWindowSurface(
        graphics->display, graphics->config,
        (EGLNativeWindowType)graphics->surface, NULL);
    if (graphics->context == EGL_NO_CONTEXT || graphics->eglSurface == EGL_NO_SURFACE ||
        !eglMakeCurrent(graphics->display, graphics->eglSurface,
                        graphics->eglSurface, graphics->context)) {
        fprintf(stderr, "ERROR egl-context code=0x%x\n", eglGetError());
        return -1;
    }
    eglSwapInterval(graphics->display, 0);
    printf("EGL vendor=%s version=%s glRenderer=%s glVersion=%s\n",
           eglQueryString(graphics->display, EGL_VENDOR),
           eglQueryString(graphics->display, EGL_VERSION),
           glGetString(GL_RENDERER),
           glGetString(GL_VERSION));
    return 0;
}

static int ResizeGraphicsSurface(GraphicsContext *graphics, int width, int height)
{
    if (!eglMakeCurrent(graphics->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        graphics->context)) {
        fprintf(stderr, "ERROR resize-release-context code=0x%x\n", eglGetError());
        return -1;
    }
    if (graphics->eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(graphics->display, graphics->eglSurface);
        graphics->eglSurface = EGL_NO_SURFACE;
    }
    if (graphics->surface) {
        gbm_surface_destroy(graphics->surface);
        graphics->surface = NULL;
    }

    graphics->surface = gbm_surface_create(
        graphics->device, (uint32_t)width, (uint32_t)height,
        GBM_FORMAT_XRGB8888,
        GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    if (!graphics->surface) {
        fprintf(stderr, "ERROR resize-gbm-surface size=%dx%d errno=%d message=%s\n",
                width, height, errno, strerror(errno));
        return -1;
    }
    graphics->eglSurface = eglCreateWindowSurface(
        graphics->display, graphics->config,
        (EGLNativeWindowType)graphics->surface, NULL);
    if (graphics->eglSurface == EGL_NO_SURFACE ||
        !eglMakeCurrent(graphics->display, graphics->eglSurface,
                        graphics->eglSurface, graphics->context)) {
        fprintf(stderr, "ERROR resize-egl-surface size=%dx%d code=0x%x\n",
                width, height, eglGetError());
        return -1;
    }
    eglSwapInterval(graphics->display, 0);
    return 0;
}

static void DestroyGraphics(GraphicsContext *graphics)
{
    if (graphics->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(graphics->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (graphics->eglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(graphics->display, graphics->eglSurface);
        }
        if (graphics->context != EGL_NO_CONTEXT) {
            eglDestroyContext(graphics->display, graphics->context);
        }
        eglTerminate(graphics->display);
    }
    if (graphics->surface) gbm_surface_destroy(graphics->surface);
    if (graphics->device) gbm_device_destroy(graphics->device);
}

static void RenderScene(int width, int height, int frame)
{
    float phase = (float)(frame % 240) / 240.0f;
    glDisable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glScissor(0, 0, width, height);
    glClearColor(0.03f, 0.04f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const GLfloat colors[4][3] = {
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 1.0f, 1.0f, 0.0f }
    };
    int halfWidth = width / 2;
    int halfHeight = height / 2;
    for (int index = 0; index < 4; index++) {
        int x = (index & 1) ? halfWidth : 0;
        int y = (index & 2) ? halfHeight : 0;
        glScissor(x + 12, y + 12, halfWidth - 24, halfHeight - 24);
        glClearColor(colors[index][0], colors[index][1], colors[index][2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    int barWidth = width / 24;
    int barX = (int)((width + barWidth) * phase) - barWidth;
    glScissor(barX, 0, barWidth, height);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

static int LockSceneFrame(int fd, GraphicsContext *graphics, SceneFrame *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->bo = gbm_surface_lock_front_buffer(graphics->surface);
    if (!frame->bo) {
        fprintf(stderr, "ERROR lock-front-buffer\n");
        return -1;
    }
    int primeFd = gbm_bo_get_fd(frame->bo);
    if (primeFd < 0) {
        fprintf(stderr, "ERROR export-dmabuf errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }
    uint32_t importedHandle = 0;
    int importResult = drmPrimeFDToHandle(fd, primeFd, &importedHandle);
    close(primeFd);
    if (importResult != 0) {
        fprintf(stderr, "ERROR import-dmabuf errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }
    uint32_t handles[4] = { importedHandle, 0, 0, 0 };
    uint32_t pitches[4] = { gbm_bo_get_stride(frame->bo), 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    if (drmModeAddFB2(fd,
                      gbm_bo_get_width(frame->bo),
                      gbm_bo_get_height(frame->bo),
                      DRM_FORMAT_XRGB8888,
                      handles, pitches, offsets,
                      &frame->fbId, 0) != 0) {
        fprintf(stderr, "ERROR addfb2-scene errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }
    return 0;
}

static void ReleaseSceneFrame(int fd, GraphicsContext *graphics, SceneFrame *frame)
{
    if (frame->fbId) drmModeRmFB(fd, frame->fbId);
    if (frame->bo) gbm_surface_release_buffer(graphics->surface, frame->bo);
    memset(frame, 0, sizeof(*frame));
}

static int CreateNativeFrame(int fd, GraphicsContext *graphics,
                             uint32_t width, uint32_t height,
                             NativeFrame *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->dmaBufFd = -1;
    frame->bo = gbm_bo_create(graphics->device, width, height,
                              GBM_FORMAT_XRGB8888,
                              GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    if (!frame->bo) {
        fprintf(stderr, "ERROR native-gbm-bo errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }
    frame->dmaBufFd = gbm_bo_get_fd(frame->bo);
    if (frame->dmaBufFd < 0) {
        fprintf(stderr, "ERROR native-export-dmabuf errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }
    uint32_t importedHandle = 0;
    if (drmPrimeFDToHandle(fd, frame->dmaBufFd, &importedHandle) != 0) {
        fprintf(stderr, "ERROR native-import-dmabuf errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }
    uint32_t handles[4] = { importedHandle, 0, 0, 0 };
    uint32_t pitches[4] = { gbm_bo_get_stride(frame->bo), 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    if (drmModeAddFB2(fd, width, height, DRM_FORMAT_XRGB8888,
                      handles, pitches, offsets, &frame->fbId, 0) != 0) {
        fprintf(stderr, "ERROR native-addfb2 errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }
    return 0;
}

static void DestroyNativeFrame(int fd, NativeFrame *frame)
{
    if (frame->fbId) drmModeRmFB(fd, frame->fbId);
    if (frame->dmaBufFd >= 0) close(frame->dmaBufFd);
    if (frame->bo) gbm_bo_destroy(frame->bo);
    memset(frame, 0, sizeof(*frame));
    frame->dmaBufFd = -1;
}

static int SetConverterFormat(int fd, enum v4l2_buf_type type,
                              uint32_t width, uint32_t height,
                              uint32_t stride, uint32_t *size)
{
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = type;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    // DRM XRGB8888 is byte-ordered B,G,R,X on this little-endian target.
    // The i.MX6 IPU V4L2 mapping names that memory layout XBGR32.
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_XBGR32;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    format.fmt.pix.bytesperline = stride;
    format.fmt.pix.sizeimage = stride * height;
    format.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    if (IoctlRetry(fd, VIDIOC_S_FMT, &format) != 0) {
        fprintf(stderr, "ERROR ipu-s-fmt type=%u errno=%d message=%s\n",
                type, errno, strerror(errno));
        return -1;
    }
    if (format.fmt.pix.width != width || format.fmt.pix.height != height ||
        format.fmt.pix.pixelformat != V4L2_PIX_FMT_XBGR32 ||
        format.fmt.pix.bytesperline != stride) {
        fprintf(stderr, "ERROR ipu-format-adjusted type=%u requested=%ux%u/%u"
                " actual=%ux%u/%u\n",
                type, width, height, stride,
                format.fmt.pix.width, format.fmt.pix.height,
                format.fmt.pix.bytesperline);
        return -1;
    }
    *size = format.fmt.pix.sizeimage;
    return 0;
}

static int RequestConverterBuffers(int fd, enum v4l2_buf_type type,
                                   uint32_t count, uint32_t *actual)
{
    struct v4l2_requestbuffers request;
    memset(&request, 0, sizeof(request));
    request.count = count;
    request.type = type;
    request.memory = V4L2_MEMORY_DMABUF;
    if (IoctlRetry(fd, VIDIOC_REQBUFS, &request) != 0 || request.count == 0) {
        fprintf(stderr, "ERROR ipu-reqbufs type=%u errno=%d message=%s\n",
                type, errno, strerror(errno));
        return -1;
    }
    *actual = request.count;
    return 0;
}

static int InitializeConverter(IpuConverter *converter,
                               uint32_t inputWidth, uint32_t inputHeight,
                               uint32_t inputStride,
                               uint32_t outputWidth, uint32_t outputHeight,
                               uint32_t outputStride)
{
    memset(converter, 0, sizeof(*converter));
    converter->fd = open("/dev/video4", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (converter->fd < 0) {
        fprintf(stderr, "ERROR open-ipu-ic errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }
    if (SetConverterFormat(converter->fd, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                           inputWidth, inputHeight, inputStride,
                           &converter->inputSize) != 0 ||
        SetConverterFormat(converter->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                           outputWidth, outputHeight, outputStride,
                           &converter->outputSize) != 0 ||
        RequestConverterBuffers(converter->fd, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                                3, &converter->inputSlots) != 0 ||
        RequestConverterBuffers(converter->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                2, &converter->outputSlots) != 0) {
        return -1;
    }
    printf("IPU_IC input=%ux%u stride=%u size=%u output=%ux%u stride=%u size=%u"
           " inputSlots=%u outputSlots=%u\n",
           inputWidth, inputHeight, inputStride, converter->inputSize,
           outputWidth, outputHeight, outputStride, converter->outputSize,
           converter->inputSlots, converter->outputSlots);
    return 0;
}

static int QueueConverterBuffer(IpuConverter *converter,
                                enum v4l2_buf_type type,
                                uint32_t index, int dmaBufFd, uint32_t size)
{
    struct v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = type;
    buffer.memory = V4L2_MEMORY_DMABUF;
    buffer.index = index;
    buffer.m.fd = dmaBufFd;
    buffer.length = size;
    buffer.bytesused = V4L2_TYPE_IS_OUTPUT(type) ? size : 0;
    buffer.field = V4L2_FIELD_NONE;
    if (IoctlRetry(converter->fd, VIDIOC_QBUF, &buffer) != 0) {
        fprintf(stderr, "ERROR ipu-qbuf type=%u index=%u errno=%d message=%s\n",
                type, index, errno, strerror(errno));
        return -1;
    }
    return 0;
}

static int DequeueConverterBuffer(IpuConverter *converter,
                                  enum v4l2_buf_type type,
                                  uint32_t *index)
{
    double deadline = MonotonicMilliseconds() + 3000.0;
    while (MonotonicMilliseconds() < deadline) {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = type;
        buffer.memory = V4L2_MEMORY_DMABUF;
        if (IoctlRetry(converter->fd, VIDIOC_DQBUF, &buffer) == 0) {
            if (buffer.flags & V4L2_BUF_FLAG_ERROR) {
                fprintf(stderr, "ERROR ipu-dqbuf-error type=%u index=%u\n",
                        type, buffer.index);
                return -1;
            }
            *index = buffer.index;
            return 0;
        }
        if (errno != EAGAIN) {
            fprintf(stderr, "ERROR ipu-dqbuf type=%u errno=%d message=%s\n",
                    type, errno, strerror(errno));
            return -1;
        }
        struct pollfd pollFd = { .fd = converter->fd, .events = POLLIN | POLLOUT };
        int timeout = (int)(deadline - MonotonicMilliseconds());
        if (timeout < 1) timeout = 1;
        if (poll(&pollFd, 1, timeout) < 0 && errno != EINTR) return -1;
    }
    fprintf(stderr, "ERROR ipu-dqbuf-timeout type=%u\n", type);
    return -1;
}

static int ConvertFrame(IpuConverter *converter, uint32_t sequence,
                        int inputDmaBufFd, int outputDmaBufFd)
{
    uint32_t inputIndex = sequence % converter->inputSlots;
    uint32_t outputIndex = sequence % converter->outputSlots;
    if (QueueConverterBuffer(converter, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                             inputIndex, inputDmaBufFd,
                             converter->inputSize) != 0 ||
        QueueConverterBuffer(converter, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                             outputIndex, outputDmaBufFd,
                             converter->outputSize) != 0) {
        return -1;
    }
    if (!converter->streaming) {
        enum v4l2_buf_type outputType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        enum v4l2_buf_type captureType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (IoctlRetry(converter->fd, VIDIOC_STREAMON, &outputType) != 0 ||
            IoctlRetry(converter->fd, VIDIOC_STREAMON, &captureType) != 0) {
            fprintf(stderr, "ERROR ipu-streamon errno=%d message=%s\n", errno, strerror(errno));
            return -1;
        }
        converter->streaming = true;
    }
    uint32_t dequeuedInput = UINT32_MAX;
    uint32_t dequeuedOutput = UINT32_MAX;
    if (DequeueConverterBuffer(converter, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                               &dequeuedOutput) != 0 ||
        DequeueConverterBuffer(converter, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                               &dequeuedInput) != 0 ||
        dequeuedInput != inputIndex || dequeuedOutput != outputIndex) {
        fprintf(stderr, "ERROR ipu-buffer-order expected=%u/%u actual=%u/%u\n",
                inputIndex, outputIndex, dequeuedInput, dequeuedOutput);
        return -1;
    }
    return 0;
}

static void DestroyConverter(IpuConverter *converter)
{
    if (converter->fd >= 0) {
        if (converter->streaming) {
            enum v4l2_buf_type outputType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            enum v4l2_buf_type captureType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            IoctlRetry(converter->fd, VIDIOC_STREAMOFF, &outputType);
            IoctlRetry(converter->fd, VIDIOC_STREAMOFF, &captureType);
        }
        close(converter->fd);
    }
    memset(converter, 0, sizeof(*converter));
    converter->fd = -1;
}

static int CreateOverlay(int fd, uint32_t width, uint32_t height, DumbOverlay *overlay)
{
    memset(overlay, 0, sizeof(*overlay));
    overlay->create.width = width;
    overlay->create.height = height;
    overlay->create.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &overlay->create) != 0) {
        fprintf(stderr, "ERROR create-overlay errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }
    uint32_t handles[4] = { overlay->create.handle, 0, 0, 0 };
    uint32_t pitches[4] = { overlay->create.pitch, 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    if (drmModeAddFB2(fd, width, height, DRM_FORMAT_XRGB8888,
                      handles, pitches, offsets, &overlay->fbId, 0) != 0) {
        fprintf(stderr, "ERROR addfb2-overlay errno=%d message=%s\n", errno, strerror(errno));
        return -1;
    }
    struct drm_mode_map_dumb map = { .handle = overlay->create.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) return -1;
    overlay->mapping = mmap(NULL, (size_t)overlay->create.size,
                            PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)map.offset);
    if (overlay->mapping == MAP_FAILED) {
        overlay->mapping = NULL;
        return -1;
    }
    for (uint32_t y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)overlay->mapping +
                                     (size_t)y * overlay->create.pitch);
        for (uint32_t x = 0; x < width; x++) {
            bool border = x < 4 || y < 4 || x + 4 >= width || y + 4 >= height;
            bool finePattern = x < width / 3 && ((x ^ y) & 1u);
            uint8_t grey = (uint8_t)(48 + ((x / 16u) % 10u) * 18u);
            if (border || finePattern) grey = 255;
            row[x] = ((uint32_t)grey << 16) | ((uint32_t)grey << 8) | grey;
        }
    }
    return 0;
}

static void DestroyOverlay(int fd, DumbOverlay *overlay)
{
    if (overlay->mapping) munmap(overlay->mapping, (size_t)overlay->create.size);
    if (overlay->fbId) drmModeRmFB(fd, overlay->fbId);
    if (overlay->create.handle) {
        struct drm_mode_destroy_dumb destroy = { .handle = overlay->create.handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    memset(overlay, 0, sizeof(*overlay));
}

static int AddPlaneLayout(int fd, drmModeAtomicReq *request,
                          uint32_t crtcId, const PlaneLayout *layout)
{
    if (!layout) return 0;
    int failed = 0;
#define ADD_PLANE(name, value) \
    do { if (AddAtomicProperty(fd, request, layout->planeId, DRM_MODE_OBJECT_PLANE, \
                               (name), (value), true) != 0) failed = 1; } while (0)
    ADD_PLANE("FB_ID", layout->fbId);
    ADD_PLANE("CRTC_ID", crtcId);
    ADD_PLANE("SRC_X", 0);
    ADD_PLANE("SRC_Y", 0);
    ADD_PLANE("SRC_W", (uint64_t)layout->sourceWidth << 16);
    ADD_PLANE("SRC_H", (uint64_t)layout->sourceHeight << 16);
    ADD_PLANE("CRTC_X", (uint64_t)(int64_t)layout->destinationX);
    ADD_PLANE("CRTC_Y", (uint64_t)(int64_t)layout->destinationY);
    ADD_PLANE("CRTC_W", layout->destinationWidth);
    ADD_PLANE("CRTC_H", layout->destinationHeight);
#undef ADD_PLANE
    if (AddAtomicProperty(fd, request, layout->planeId, DRM_MODE_OBJECT_PLANE,
                          "zpos", layout->zPosition, false) != 0) failed = 1;
    return failed ? -1 : 0;
}

static int ConfigureDisplay(int fd, const DisplayPath *path,
                            const PlaneLayout *primary,
                            const PlaneLayout *overlay, bool testOnly)
{
    uint32_t modeBlob = 0;
    if (drmModeCreatePropertyBlob(fd, &path->mode, sizeof(path->mode), &modeBlob) != 0) {
        return -1;
    }
    drmModeAtomicReq *request = drmModeAtomicAlloc();
    if (!request) {
        drmModeDestroyPropertyBlob(fd, modeBlob);
        return -1;
    }
    int failed = 0;
#define ADD(object, type, name, value) \
    do { if (AddAtomicProperty(fd, request, (object), (type), (name), (value), true) != 0) failed = 1; } while (0)
    ADD(path->connector->connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", path->crtcId);
    ADD(path->crtcId, DRM_MODE_OBJECT_CRTC, "MODE_ID", modeBlob);
    ADD(path->crtcId, DRM_MODE_OBJECT_CRTC, "ACTIVE", 1);
#undef ADD
    if (AddPlaneLayout(fd, request, path->crtcId, primary) != 0) failed = 1;
    if (AddPlaneLayout(fd, request, path->crtcId, overlay) != 0) failed = 1;
    uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    if (testOnly) flags |= DRM_MODE_ATOMIC_TEST_ONLY;
    int result = failed ? -1 : drmModeAtomicCommit(fd, request, flags, NULL);
    if (result != 0) {
        fprintf(stderr, "%s atomic-modeset testOnly=%d errno=%d message=%s\n",
                testOnly ? "ATOMIC_TEST_REJECTED" : "ERROR",
                testOnly, errno, strerror(errno));
    }
    drmModeAtomicFree(request);
    drmModeDestroyPropertyBlob(fd, modeBlob);
    return result;
}

static int PresentFrame(int fd, uint32_t primaryPlaneId, uint32_t fbId)
{
    drmModeAtomicReq *request = drmModeAtomicAlloc();
    if (!request) return -1;
    int result = AddAtomicProperty(fd, request, primaryPlaneId,
                                   DRM_MODE_OBJECT_PLANE, "FB_ID", fbId, true);
    if (result == 0) result = drmModeAtomicCommit(fd, request, 0, NULL);
    if (result != 0) {
        fprintf(stderr, "ERROR atomic-frame errno=%d message=%s\n", errno, strerror(errno));
    }
    drmModeAtomicFree(request);
    return result;
}

static void DisableDisplay(int fd, const DisplayPath *path)
{
    drmModeAtomicReq *request = drmModeAtomicAlloc();
    if (!request) return;
    AddAtomicProperty(fd, request, path->primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                      "FB_ID", 0, false);
    AddAtomicProperty(fd, request, path->primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                      "CRTC_ID", 0, false);
    AddAtomicProperty(fd, request, path->overlayPlaneId, DRM_MODE_OBJECT_PLANE,
                      "FB_ID", 0, false);
    AddAtomicProperty(fd, request, path->overlayPlaneId, DRM_MODE_OBJECT_PLANE,
                      "CRTC_ID", 0, false);
    AddAtomicProperty(fd, request, path->connector->connector_id,
                      DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", 0, false);
    AddAtomicProperty(fd, request, path->crtcId, DRM_MODE_OBJECT_CRTC,
                      "ACTIVE", 0, false);
    AddAtomicProperty(fd, request, path->crtcId, DRM_MODE_OBJECT_CRTC,
                      "MODE_ID", 0, false);
    drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
    drmModeAtomicFree(request);
}

int main(int argc, char **argv)
{
    const char *device = argc > 1 ? argv[1] : "/dev/dri/card1";
    int seconds = argc > 2 ? atoi(argv[2]) : 10;
    double density = argc > 3 ? atof(argv[3]) : 0.5;
    double transitionDensity = argc > 4 ? atof(argv[4]) : 0.0;
    if (seconds < 1 || seconds > 300 || density < 0.1 || density > 1.0 ||
        transitionDensity < 0.0 || transitionDensity > 1.0 ||
        (transitionDensity > 0.0 && transitionDensity < 0.1)) {
        fprintf(stderr, "usage: %s [device] [seconds 1..300]"
                " [density 0.1..1.0] [transition-density 0.1..1.0]\n", argv[0]);
        return 2;
    }
    signal(SIGINT, HandleSignal);
    signal(SIGTERM, HandleSignal);

    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "ERROR open device=%s errno=%d message=%s\n",
                device, errno, strerror(errno));
        return 1;
    }
    if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0 ||
        drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        fprintf(stderr, "ERROR required-client-cap errno=%d message=%s\n", errno, strerror(errno));
        close(fd);
        return 1;
    }
    drmSetMaster(fd);

    DisplayPath path;
    if (FindDisplayPath(fd, &path) != 0) {
        close(fd);
        return 1;
    }
    int sourceWidth = ((int)floor(path.mode.hdisplay * density) / 2) * 2;
    int sourceHeight = ((int)floor(path.mode.vdisplay * density) / 2) * 2;
    if (sourceWidth < 64) sourceWidth = 64;
    if (sourceHeight < 64) sourceHeight = 64;
    printf("MICROFX_IPU_DEMO schema=1 connector=%u crtc=%u primary=%u overlay=%u"
           " mode=%s output=%ux%u@%u source=%dx%d density=%.3f"
           " transitionDensity=%.3f seconds=%d\n",
           path.connector->connector_id, path.crtcId,
           path.primaryPlaneId, path.overlayPlaneId,
           path.mode.name, path.mode.hdisplay, path.mode.vdisplay,
           path.mode.vrefresh, sourceWidth, sourceHeight, density,
           transitionDensity, seconds);

    GraphicsContext graphics;
    DumbOverlay overlay = {0};
    DumbOverlay nativeBackground = {0};
    NativeFrame nativeFrames[2] = {
        { .dmaBufFd = -1 },
        { .dmaBufFd = -1 }
    };
    IpuConverter converter = { .fd = -1 };
    SceneFrame current = {0};
    int exitCode = 1;
    bool displayConfigured = false;
    bool converterActive = false;
    uint32_t currentNativeIndex = 0;
    if (InitializeGraphics(fd, sourceWidth, sourceHeight, &graphics) != 0) goto cleanup_path;
    uint32_t overlayWidth = path.mode.hdisplay / 3u;
    if (overlayWidth > 512) overlayWidth = 512;
    if (overlayWidth < 192) overlayWidth = 192;
    uint32_t overlayHeight = path.mode.vdisplay / 12u;
    if (overlayHeight > 120) overlayHeight = 120;
    if (overlayHeight < 64) overlayHeight = 64;
    if (CreateOverlay(fd, overlayWidth, overlayHeight, &overlay) != 0) goto cleanup_graphics;

    RenderScene(sourceWidth, sourceHeight, 0);
    if (!eglSwapBuffers(graphics.display, graphics.eglSurface)) goto cleanup_overlay;
    glFinish();
    if (LockSceneFrame(fd, &graphics, &current) != 0) goto cleanup_overlay;
    PlaneLayout scaledPrimary = {
        .planeId = path.primaryPlaneId,
        .fbId = current.fbId,
        .sourceWidth = (uint32_t)sourceWidth,
        .sourceHeight = (uint32_t)sourceHeight,
        .destinationWidth = path.mode.hdisplay,
        .destinationHeight = path.mode.vdisplay,
        .zPosition = 0
    };
    PlaneLayout nativeOverlay = {
        .planeId = path.overlayPlaneId,
        .fbId = overlay.fbId,
        .sourceWidth = overlay.create.width,
        .sourceHeight = overlay.create.height,
        .destinationX = 24,
        .destinationY = 24,
        .destinationWidth = overlay.create.width,
        .destinationHeight = overlay.create.height,
        .zPosition = 1
    };
    uint32_t scenePlaneId = path.primaryPlaneId;
    bool nativeOverlayAbove = false;
    if (ConfigureDisplay(fd, &path, &scaledPrimary, NULL, true) != 0) {
        printf("MICROFX_EVIDENCE primaryScalingTest=no\n");
        if (CreateOverlay(fd, path.mode.hdisplay, path.mode.vdisplay,
                          &nativeBackground) != 0) goto cleanup_frame;
        PlaneLayout nativePrimary = {
            .planeId = path.primaryPlaneId,
            .fbId = nativeBackground.fbId,
            .sourceWidth = nativeBackground.create.width,
            .sourceHeight = nativeBackground.create.height,
            .destinationWidth = path.mode.hdisplay,
            .destinationHeight = path.mode.vdisplay,
            .zPosition = 0
        };
        PlaneLayout scaledOverlay = {
            .planeId = path.overlayPlaneId,
            .fbId = current.fbId,
            .sourceWidth = (uint32_t)sourceWidth,
            .sourceHeight = (uint32_t)sourceHeight,
            .destinationWidth = path.mode.hdisplay,
            .destinationHeight = path.mode.vdisplay,
            .zPosition = 1
        };
        if (ConfigureDisplay(fd, &path, &nativePrimary, &scaledOverlay, true) != 0) {
            printf("MICROFX_EVIDENCE overlayScalingTest=no\n");
            DestroyOverlay(fd, &nativeBackground);
            if (CreateNativeFrame(fd, &graphics,
                                  path.mode.hdisplay, path.mode.vdisplay,
                                  &nativeFrames[0]) != 0 ||
                CreateNativeFrame(fd, &graphics,
                                  path.mode.hdisplay, path.mode.vdisplay,
                                  &nativeFrames[1]) != 0 ||
                InitializeConverter(&converter,
                                    (uint32_t)sourceWidth,
                                    (uint32_t)sourceHeight,
                                    gbm_bo_get_stride(current.bo),
                                    path.mode.hdisplay,
                                    path.mode.vdisplay,
                                    gbm_bo_get_stride(nativeFrames[0].bo)) != 0) {
                goto cleanup_frame;
            }
            int sourceDmaBuf = gbm_bo_get_fd(current.bo);
            if (sourceDmaBuf < 0 ||
                ConvertFrame(&converter, 0, sourceDmaBuf,
                             nativeFrames[0].dmaBufFd) != 0) {
                if (sourceDmaBuf >= 0) close(sourceDmaBuf);
                goto cleanup_frame;
            }
            close(sourceDmaBuf);
            PlaneLayout convertedPrimary = {
                .planeId = path.primaryPlaneId,
                .fbId = nativeFrames[0].fbId,
                .sourceWidth = path.mode.hdisplay,
                .sourceHeight = path.mode.vdisplay,
                .destinationWidth = path.mode.hdisplay,
                .destinationHeight = path.mode.vdisplay,
                .zPosition = 0
            };
            if (ConfigureDisplay(fd, &path, &convertedPrimary,
                                 &nativeOverlay, true) != 0) {
                printf("MICROFX_EVIDENCE ipuIcCompositionTest=no\n");
                goto cleanup_frame;
            }
            printf("MICROFX_EVIDENCE ipuIcCompositionTest=yes\n");
            if (ConfigureDisplay(fd, &path, &convertedPrimary,
                                 &nativeOverlay, false) != 0) {
                goto cleanup_frame;
            }
            converterActive = true;
            nativeOverlayAbove = true;
            scenePlaneId = path.primaryPlaneId;
            currentNativeIndex = 0;
        }
        if (!converterActive) {
            printf("MICROFX_EVIDENCE overlayScalingTest=yes\n");
            if (ConfigureDisplay(fd, &path, &nativePrimary, &scaledOverlay, false) != 0) {
                goto cleanup_frame;
            }
            scenePlaneId = path.overlayPlaneId;
        }
    } else {
        printf("MICROFX_EVIDENCE primaryScalingTest=yes\n");
        if (ConfigureDisplay(fd, &path, &scaledPrimary, &nativeOverlay, true) != 0) {
            printf("MICROFX_EVIDENCE overlayCompositionTest=no\n");
            goto cleanup_frame;
        }
        printf("MICROFX_EVIDENCE overlayCompositionTest=yes\n");
        if (ConfigureDisplay(fd, &path, &scaledPrimary, &nativeOverlay, false) != 0) {
            goto cleanup_frame;
        }
        nativeOverlayAbove = true;
    }
    displayConfigured = true;
    if (converterActive) ReleaseSceneFrame(fd, &graphics, &current);

    double start = MonotonicMilliseconds();
    double cpuStart = ProcessCpuMilliseconds();
    double renderTotal = 0.0;
    double conversionTotal = 0.0;
    double conversionMaximum = 0.0;
    double commitTotal = 0.0;
    double commitMaximum = 0.0;
    bool transitionCompleted = false;
    double transitionMilliseconds = 0.0;
    int frames = 1;
    while (!stopRequested && MonotonicMilliseconds() - start < seconds * 1000.0) {
        if (transitionDensity > 0.0 && !transitionCompleted &&
            MonotonicMilliseconds() - start >= seconds * 500.0) {
            if (!converterActive) {
                fprintf(stderr, "ERROR dynamic-transition-requires-ipu-path\n");
                break;
            }
            int nextSourceWidth =
                ((int)floor(path.mode.hdisplay * transitionDensity) / 2) * 2;
            int nextSourceHeight =
                ((int)floor(path.mode.vdisplay * transitionDensity) / 2) * 2;
            if (nextSourceWidth < 64) nextSourceWidth = 64;
            if (nextSourceHeight < 64) nextSourceHeight = 64;

            double transitionStart = MonotonicMilliseconds();
            DestroyConverter(&converter);
            if (ResizeGraphicsSurface(&graphics, nextSourceWidth,
                                      nextSourceHeight) != 0) break;
            RenderScene(nextSourceWidth, nextSourceHeight, frames);
            if (!eglSwapBuffers(graphics.display, graphics.eglSurface)) break;
            glFinish();
            SceneFrame transitionFrame;
            if (LockSceneFrame(fd, &graphics, &transitionFrame) != 0) break;
            if (InitializeConverter(&converter,
                                    (uint32_t)nextSourceWidth,
                                    (uint32_t)nextSourceHeight,
                                    gbm_bo_get_stride(transitionFrame.bo),
                                    path.mode.hdisplay,
                                    path.mode.vdisplay,
                                    gbm_bo_get_stride(nativeFrames[0].bo)) != 0) {
                ReleaseSceneFrame(fd, &graphics, &transitionFrame);
                break;
            }
            uint32_t nextNativeIndex = (currentNativeIndex + 1u) % 2u;
            int sourceDmaBuf = gbm_bo_get_fd(transitionFrame.bo);
            int conversionResult = sourceDmaBuf < 0 ? -1 :
                ConvertFrame(&converter, (uint32_t)frames, sourceDmaBuf,
                             nativeFrames[nextNativeIndex].dmaBufFd);
            if (sourceDmaBuf >= 0) close(sourceDmaBuf);
            if (conversionResult != 0 ||
                PresentFrame(fd, scenePlaneId,
                             nativeFrames[nextNativeIndex].fbId) != 0) {
                ReleaseSceneFrame(fd, &graphics, &transitionFrame);
                break;
            }
            ReleaseSceneFrame(fd, &graphics, &transitionFrame);
            currentNativeIndex = nextNativeIndex;
            sourceWidth = nextSourceWidth;
            sourceHeight = nextSourceHeight;
            density = transitionDensity;
            transitionMilliseconds = MonotonicMilliseconds() - transitionStart;
            transitionCompleted = true;
            printf("MICROFX_DENSITY_TRANSITION old_frame_held=yes modeset=no"
                   " source=%dx%d density=%.3f elapsedMs=%.3f\n",
                   sourceWidth, sourceHeight, density, transitionMilliseconds);
            fflush(stdout);
            continue;
        }
        double renderStart = MonotonicMilliseconds();
        RenderScene(sourceWidth, sourceHeight, frames);
        if (!eglSwapBuffers(graphics.display, graphics.eglSurface)) break;
        glFinish();
        SceneFrame next;
        if (LockSceneFrame(fd, &graphics, &next) != 0) break;
        double renderEnd = MonotonicMilliseconds();
        uint32_t presentedFb = next.fbId;
        if (converterActive) {
            uint32_t nextNativeIndex = (currentNativeIndex + 1u) % 2u;
            int sourceDmaBuf = gbm_bo_get_fd(next.bo);
            double conversionStart = MonotonicMilliseconds();
            int conversionResult = sourceDmaBuf < 0 ? -1 :
                ConvertFrame(&converter, (uint32_t)frames,
                             sourceDmaBuf,
                             nativeFrames[nextNativeIndex].dmaBufFd);
            if (sourceDmaBuf >= 0) close(sourceDmaBuf);
            double conversionEnd = MonotonicMilliseconds();
            if (conversionResult != 0) {
                ReleaseSceneFrame(fd, &graphics, &next);
                break;
            }
            double conversion = conversionEnd - conversionStart;
            conversionTotal += conversion;
            if (conversion > conversionMaximum) conversionMaximum = conversion;
            presentedFb = nativeFrames[nextNativeIndex].fbId;
            currentNativeIndex = nextNativeIndex;
        }
        double commitStart = MonotonicMilliseconds();
        if (PresentFrame(fd, scenePlaneId, presentedFb) != 0) {
            ReleaseSceneFrame(fd, &graphics, &next);
            break;
        }
        double commitEnd = MonotonicMilliseconds();
        if (converterActive) {
            ReleaseSceneFrame(fd, &graphics, &next);
        } else {
            ReleaseSceneFrame(fd, &graphics, &current);
            current = next;
        }
        renderTotal += renderEnd - renderStart;
        double commit = commitEnd - commitStart;
        commitTotal += commit;
        if (commit > commitMaximum) commitMaximum = commit;
        frames++;
    }
    double elapsed = MonotonicMilliseconds() - start;
    double cpuElapsed = ProcessCpuMilliseconds() - cpuStart;
    if (frames < 2) {
        fprintf(stderr, "ERROR no-presented-frames\n");
        goto cleanup_frame;
    }
    printf("PROFILE frames=%d elapsedMs=%.3f fps=%.3f renderFinishAvgMs=%.3f"
           " processCpuMs=%.3f processCpuPercent=%.3f"
           " ipuConvertAvgMs=%.3f ipuConvertMaxMs=%.3f"
           " atomicCommitAvgMs=%.3f atomicCommitMaxMs=%.3f\n",
           frames, elapsed, frames * 1000.0 / elapsed,
           renderTotal / (frames - 1), cpuElapsed,
           elapsed > 0.0 ? cpuElapsed * 100.0 / elapsed : 0.0,
           conversionTotal / (frames - 1), conversionMaximum,
           commitTotal / (frames - 1), commitMaximum);
    printf("MICROFX_EVIDENCE scaling=%s\n",
           (sourceWidth != path.mode.hdisplay || sourceHeight != path.mode.vdisplay) ?
               "yes" : "not-requested");
    printf("MICROFX_EVIDENCE dmaBufImport=yes\n");
    printf("MICROFX_EVIDENCE ipuImageConverter=%s\n",
           converterActive ? "yes" : "not-used");
    printf("MICROFX_EVIDENCE atomicCommit=yes\n");
    printf("MICROFX_EVIDENCE pageFlipSync=yes\n");
    printf("MICROFX_EVIDENCE nativeOverlayAbove=%s\n",
           nativeOverlayAbove ? "yes" : "no");
    printf("MICROFX_EVIDENCE cpuCopyPrimary=no\n");
    printf("MICROFX_EVIDENCE globalAlpha=no\n");
    printf("MICROFX_EVIDENCE dynamicTransition=%s\n",
           transitionDensity <= 0.0 ? "not-requested" :
           transitionCompleted ? "yes" : "no");
    if (transitionCompleted) {
        printf("MICROFX_EVIDENCE transitionModeset=no\n");
        printf("MICROFX_EVIDENCE transitionOldFrameHeld=yes\n");
        printf("MICROFX_EVIDENCE transitionElapsedMs=%.3f\n",
               transitionMilliseconds);
    }
    exitCode = 0;

cleanup_frame:
    if (displayConfigured) DisableDisplay(fd, &path);
    DestroyConverter(&converter);
    ReleaseSceneFrame(fd, &graphics, &current);
    DestroyOverlay(fd, &nativeBackground);
    DestroyNativeFrame(fd, &nativeFrames[0]);
    DestroyNativeFrame(fd, &nativeFrames[1]);
cleanup_overlay:
    DestroyOverlay(fd, &overlay);
cleanup_graphics:
    DestroyGraphics(&graphics);
cleanup_path:
    drmModeFreeConnector(path.connector);
    drmDropMaster(fd);
    close(fd);
    return exitCode;
}
