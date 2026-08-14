// microFX i.MX6 IPU presentation backend.
// Included by the patched raylib DRM platform so it can reuse the platform's
// DRM master, GBM device, EGL context and connector selection.

#include <errno.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>

#include <drm_fourcc.h>

typedef struct {
    struct gbm_bo *bo;
    int dmaBufFd;
    uint32_t fbId;
} MicroFxIpuNativeFrame;

typedef struct {
    int fd;
    bool streaming;
    uint32_t inputSize;
    uint32_t outputSize;
    uint32_t inputSlots;
    uint32_t outputSlots;
} MicroFxIpuConverter;

typedef struct {
    bool requested;
    bool initialized;
    bool displayConfigured;
    bool failed;
    int drmFd;
    uint32_t outputWidth;
    uint32_t outputHeight;
    uint32_t primaryPlaneId;
    uint32_t currentNativeIndex;
    uint32_t sequence;
    MicroFxIpuNativeFrame nativeFrames[2];
    MicroFxIpuConverter converter;
    double convertTotalMs;
    double convertMaximumMs;
    double flipTotalMs;
    unsigned int profileFrames;
} MicroFxIpuState;

static MicroFxIpuState microFxIpu = {
    .drmFd = -1,
    .converter = { .fd = -1 },
    .nativeFrames = {
        { .dmaBufFd = -1 },
        { .dmaBufFd = -1 }
    }
};

static bool MicroFxIpuRequested(void)
{
    const char *value = getenv("MICROFX_IPU_SCALER");
    return value && strcmp(value, "1") == 0;
}

static double MicroFxIpuClockMs(void)
{
    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec*1000.0 + (double)now.tv_nsec/1000000.0;
}

static int MicroFxIpuIoctl(int fd, unsigned long request, void *argument)
{
    int result = 0;
    do result = ioctl(fd, request, argument); while (result < 0 && errno == EINTR);
    return result;
}

static uint32_t MicroFxIpuFindProperty(uint32_t objectId,
                                       uint32_t objectType,
                                       const char *name)
{
    drmModeObjectProperties *properties = drmModeObjectGetProperties(
        microFxIpu.drmFd, objectId, objectType);
    if (!properties) return 0;
    uint32_t result = 0;
    for (uint32_t index = 0; index < properties->count_props; index++) {
        drmModePropertyRes *property = drmModeGetProperty(
            microFxIpu.drmFd, properties->props[index]);
        if (!property) continue;
        if (strcmp(property->name, name) == 0) result = property->prop_id;
        drmModeFreeProperty(property);
        if (result) break;
    }
    drmModeFreeObjectProperties(properties);
    return result;
}

static bool MicroFxIpuPlaneIsPrimary(uint32_t planeId)
{
    drmModeObjectProperties *properties = drmModeObjectGetProperties(
        microFxIpu.drmFd, planeId, DRM_MODE_OBJECT_PLANE);
    if (!properties) return false;
    bool primary = false;
    for (uint32_t index = 0; index < properties->count_props; index++) {
        drmModePropertyRes *property = drmModeGetProperty(
            microFxIpu.drmFd, properties->props[index]);
        if (!property) continue;
        if (strcmp(property->name, "type") == 0 &&
            properties->prop_values[index] == DRM_PLANE_TYPE_PRIMARY) {
            primary = true;
        }
        drmModeFreeProperty(property);
        if (primary) break;
    }
    drmModeFreeObjectProperties(properties);
    return primary;
}

static int MicroFxIpuFindPrimaryPlane(uint32_t crtcId)
{
    drmModeRes *resources = drmModeGetResources(microFxIpu.drmFd);
    if (!resources) return -1;
    int crtcIndex = -1;
    for (int index = 0; index < resources->count_crtcs; index++) {
        if (resources->crtcs[index] == crtcId) crtcIndex = index;
    }
    drmModeFreeResources(resources);
    if (crtcIndex < 0) return -1;
    drmModePlaneRes *planes = drmModeGetPlaneResources(microFxIpu.drmFd);
    if (!planes) return -1;
    for (uint32_t index = 0; index < planes->count_planes; index++) {
        drmModePlane *plane = drmModeGetPlane(
            microFxIpu.drmFd, planes->planes[index]);
        if (!plane) continue;
        bool compatible = (plane->possible_crtcs & (1u << crtcIndex)) != 0;
        if (compatible && MicroFxIpuPlaneIsPrimary(plane->plane_id)) {
            microFxIpu.primaryPlaneId = plane->plane_id;
        }
        drmModeFreePlane(plane);
        if (microFxIpu.primaryPlaneId) break;
    }
    drmModeFreePlaneResources(planes);
    return microFxIpu.primaryPlaneId ? 0 : -1;
}

static int MicroFxIpuAtomicPresent(uint32_t fbId)
{
    uint32_t propertyId = MicroFxIpuFindProperty(
        microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE, "FB_ID");
    drmModeAtomicReq *request = propertyId ? drmModeAtomicAlloc() : NULL;
    if (!request) return -1;
    int result = drmModeAtomicAddProperty(
        request, microFxIpu.primaryPlaneId, propertyId, fbId) < 0 ? -1 :
        drmModeAtomicCommit(microFxIpu.drmFd, request, 0, NULL);
    drmModeAtomicFree(request);
    return result;
}

static int MicroFxIpuAtomicAdd(drmModeAtomicReq *request,
                               uint32_t objectId, uint32_t objectType,
                               const char *name, uint64_t value)
{
    uint32_t propertyId = MicroFxIpuFindProperty(objectId, objectType, name);
    if (!propertyId) {
        errno = ENOENT;
        return -1;
    }
    return drmModeAtomicAddProperty(request, objectId, propertyId, value) < 0
        ? -1 : 0;
}

static int MicroFxIpuAtomicConfigure(drmModeCrtc *crtc,
                                     drmModeConnector *connector,
                                     drmModeModeInfo *mode,
                                     uint32_t fbId, bool testOnly)
{
    (void)connector;
    if (!crtc->mode_valid || crtc->mode.hdisplay != mode->hdisplay ||
        crtc->mode.vdisplay != mode->vdisplay) {
        errno = ERANGE;
        return -1;
    }
    drmModeAtomicReq *request = drmModeAtomicAlloc();
    if (!request) return -1;
    int failed = 0;
#define MICROFX_IPU_ADD(object, type, name, value) \
    do { if (MicroFxIpuAtomicAdd(request, (object), (type), (name), \
                                 (value)) != 0) failed = 1; } while (0)
    MICROFX_IPU_ADD(microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                    "FB_ID", fbId);
    MICROFX_IPU_ADD(microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                    "CRTC_ID", crtc->crtc_id);
    MICROFX_IPU_ADD(microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                    "SRC_X", 0);
    MICROFX_IPU_ADD(microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                    "SRC_Y", 0);
    MICROFX_IPU_ADD(microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                    "SRC_W", (uint64_t)microFxIpu.outputWidth << 16);
    MICROFX_IPU_ADD(microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                    "SRC_H", (uint64_t)microFxIpu.outputHeight << 16);
    MICROFX_IPU_ADD(microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                    "CRTC_X", 0);
    MICROFX_IPU_ADD(microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                    "CRTC_Y", 0);
    MICROFX_IPU_ADD(microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                    "CRTC_W", microFxIpu.outputWidth);
    MICROFX_IPU_ADD(microFxIpu.primaryPlaneId, DRM_MODE_OBJECT_PLANE,
                    "CRTC_H", microFxIpu.outputHeight);
#undef MICROFX_IPU_ADD
    uint32_t flags = 0;
    if (testOnly) flags |= DRM_MODE_ATOMIC_TEST_ONLY;
    int result = failed ? -1 :
        drmModeAtomicCommit(microFxIpu.drmFd, request, flags, NULL);
    drmModeAtomicFree(request);
    return result;
}

static int MicroFxIpuCreateNativeFrame(struct gbm_device *device,
                                       uint32_t width, uint32_t height,
                                       MicroFxIpuNativeFrame *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->dmaBufFd = -1;
    frame->bo = gbm_bo_create(device, width, height, GBM_FORMAT_XRGB8888,
                              GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    if (!frame->bo) return -1;
    frame->dmaBufFd = gbm_bo_get_fd(frame->bo);
    if (frame->dmaBufFd < 0) return -1;
    uint32_t handles[4] = { gbm_bo_get_handle(frame->bo).u32, 0, 0, 0 };
    uint32_t pitches[4] = { gbm_bo_get_stride(frame->bo), 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    return drmModeAddFB2(microFxIpu.drmFd, width, height, DRM_FORMAT_XRGB8888,
                         handles, pitches, offsets, &frame->fbId, 0);
}

static void MicroFxIpuDestroyNativeFrame(MicroFxIpuNativeFrame *frame)
{
    if (frame->fbId) drmModeRmFB(microFxIpu.drmFd, frame->fbId);
    if (frame->dmaBufFd >= 0) close(frame->dmaBufFd);
    if (frame->bo) gbm_bo_destroy(frame->bo);
    memset(frame, 0, sizeof(*frame));
    frame->dmaBufFd = -1;
}

static int MicroFxIpuSetFormat(int fd, enum v4l2_buf_type type,
                               uint32_t width, uint32_t height,
                               uint32_t stride, uint32_t *size)
{
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = type;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    // DRM XRGB8888 is byte-ordered B,G,R,X on little-endian i.MX6. The IPU
    // V4L2 format table names that same memory layout XBGR32.
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_XBGR32;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    format.fmt.pix.bytesperline = stride;
    format.fmt.pix.sizeimage = stride*height;
    format.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    if (MicroFxIpuIoctl(fd, VIDIOC_S_FMT, &format) != 0 ||
        format.fmt.pix.width != width || format.fmt.pix.height != height ||
        format.fmt.pix.pixelformat != V4L2_PIX_FMT_XBGR32 ||
        format.fmt.pix.bytesperline != stride) return -1;
    *size = format.fmt.pix.sizeimage;
    return 0;
}

static int MicroFxIpuRequestBuffers(int fd, enum v4l2_buf_type type,
                                    uint32_t count, uint32_t *actual)
{
    struct v4l2_requestbuffers request;
    memset(&request, 0, sizeof(request));
    request.count = count;
    request.type = type;
    request.memory = V4L2_MEMORY_DMABUF;
    if (MicroFxIpuIoctl(fd, VIDIOC_REQBUFS, &request) != 0 || request.count == 0) {
        return -1;
    }
    *actual = request.count;
    return 0;
}

static void MicroFxIpuDestroyConverter(void)
{
    MicroFxIpuConverter *converter = &microFxIpu.converter;
    if (converter->fd >= 0) {
        if (converter->streaming) {
            enum v4l2_buf_type outputType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            enum v4l2_buf_type captureType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            MicroFxIpuIoctl(converter->fd, VIDIOC_STREAMOFF, &outputType);
            MicroFxIpuIoctl(converter->fd, VIDIOC_STREAMOFF, &captureType);
        }
        close(converter->fd);
    }
    memset(converter, 0, sizeof(*converter));
    converter->fd = -1;
}

static int MicroFxIpuInitializeConverter(uint32_t inputWidth,
                                         uint32_t inputHeight,
                                         uint32_t inputStride)
{
    MicroFxIpuDestroyConverter();
    MicroFxIpuConverter *converter = &microFxIpu.converter;
    converter->fd = open("/dev/video4", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (converter->fd < 0 ||
        MicroFxIpuSetFormat(converter->fd, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                            inputWidth, inputHeight, inputStride,
                            &converter->inputSize) != 0 ||
        MicroFxIpuSetFormat(converter->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                            microFxIpu.outputWidth, microFxIpu.outputHeight,
                            gbm_bo_get_stride(microFxIpu.nativeFrames[0].bo),
                            &converter->outputSize) != 0 ||
        MicroFxIpuRequestBuffers(converter->fd, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                                 3, &converter->inputSlots) != 0 ||
        MicroFxIpuRequestBuffers(converter->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                 2, &converter->outputSlots) != 0) {
        fprintf(stderr, "MICROFX_IPU fatal stage=converter-init input=%ux%u"
                " stride=%u errno=%d message=%s\n",
                inputWidth, inputHeight, inputStride, errno, strerror(errno));
        MicroFxIpuDestroyConverter();
        return -1;
    }
    printf("MICROFX_IPU converter input=%ux%u stride=%u output=%ux%u stride=%u\n",
           inputWidth, inputHeight, inputStride,
           microFxIpu.outputWidth, microFxIpu.outputHeight,
           gbm_bo_get_stride(microFxIpu.nativeFrames[0].bo));
    fflush(stdout);
    return 0;
}

static int MicroFxIpuQueue(enum v4l2_buf_type type, uint32_t index,
                           int dmaBufFd, uint32_t size)
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
    return MicroFxIpuIoctl(microFxIpu.converter.fd, VIDIOC_QBUF, &buffer);
}

static int MicroFxIpuDequeue(enum v4l2_buf_type type, uint32_t *index)
{
    double deadline = MicroFxIpuClockMs() + 3000.0;
    while (MicroFxIpuClockMs() < deadline) {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = type;
        buffer.memory = V4L2_MEMORY_DMABUF;
        if (MicroFxIpuIoctl(microFxIpu.converter.fd, VIDIOC_DQBUF, &buffer) == 0) {
            if (buffer.flags & V4L2_BUF_FLAG_ERROR) return -1;
            *index = buffer.index;
            return 0;
        }
        if (errno != EAGAIN) return -1;
        struct pollfd pollFd = {
            .fd = microFxIpu.converter.fd,
            .events = POLLIN | POLLOUT
        };
        int timeout = (int)(deadline - MicroFxIpuClockMs());
        if (timeout < 1) timeout = 1;
        if (poll(&pollFd, 1, timeout) < 0 && errno != EINTR) return -1;
    }
    errno = ETIMEDOUT;
    return -1;
}

static int MicroFxIpuConvert(int inputDmaBufFd, int outputDmaBufFd)
{
    MicroFxIpuConverter *converter = &microFxIpu.converter;
    uint32_t inputIndex = microFxIpu.sequence % converter->inputSlots;
    uint32_t outputIndex = microFxIpu.sequence % converter->outputSlots;
    if (MicroFxIpuQueue(V4L2_BUF_TYPE_VIDEO_OUTPUT, inputIndex,
                        inputDmaBufFd, converter->inputSize) != 0 ||
        MicroFxIpuQueue(V4L2_BUF_TYPE_VIDEO_CAPTURE, outputIndex,
                        outputDmaBufFd, converter->outputSize) != 0) return -1;
    if (!converter->streaming) {
        enum v4l2_buf_type outputType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        enum v4l2_buf_type captureType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (MicroFxIpuIoctl(converter->fd, VIDIOC_STREAMON, &outputType) != 0 ||
            MicroFxIpuIoctl(converter->fd, VIDIOC_STREAMON, &captureType) != 0) {
            return -1;
        }
        converter->streaming = true;
    }
    uint32_t dequeuedInput = UINT32_MAX;
    uint32_t dequeuedOutput = UINT32_MAX;
    if (MicroFxIpuDequeue(V4L2_BUF_TYPE_VIDEO_CAPTURE, &dequeuedOutput) != 0 ||
        MicroFxIpuDequeue(V4L2_BUF_TYPE_VIDEO_OUTPUT, &dequeuedInput) != 0 ||
        dequeuedInput != inputIndex || dequeuedOutput != outputIndex) return -1;
    microFxIpu.sequence++;
    return 0;
}

static int MicroFxIpuInitialize(int drmFd, struct gbm_device *device,
                                uint32_t crtcId,
                                uint32_t outputWidth, uint32_t outputHeight)
{
    microFxIpu.requested = MicroFxIpuRequested();
    if (!microFxIpu.requested) return 0;
    microFxIpu.drmFd = drmFd;
    microFxIpu.outputWidth = outputWidth;
    microFxIpu.outputHeight = outputHeight;
    if (drmSetClientCap(drmFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0 ||
        drmSetClientCap(drmFd, DRM_CLIENT_CAP_ATOMIC, 1) != 0 ||
        MicroFxIpuFindPrimaryPlane(crtcId) != 0 ||
        MicroFxIpuCreateNativeFrame(device, outputWidth, outputHeight,
                                    &microFxIpu.nativeFrames[0]) != 0 ||
        MicroFxIpuCreateNativeFrame(device, outputWidth, outputHeight,
                                    &microFxIpu.nativeFrames[1]) != 0) {
        fprintf(stderr, "MICROFX_IPU fatal stage=native-buffers output=%ux%u"
                " errno=%d message=%s\n",
                outputWidth, outputHeight, errno, strerror(errno));
        return -1;
    }
    microFxIpu.initialized = true;
    printf("MICROFX_IPU enabled output=%ux%u buffers=2 primary_plane=%u device=/dev/video4\n",
           outputWidth, outputHeight, microFxIpu.primaryPlaneId);
    fflush(stdout);
    return 0;
}

static int MicroFxIpuPresent(EGLDisplay display, EGLSurface surface,
                             struct gbm_surface *gbmSurface,
                             drmModeCrtc *crtc, drmModeConnector *connector,
                             drmModeModeInfo *mode)
{
    if (!microFxIpu.initialized || microFxIpu.failed) return -1;
    if (!eglSwapBuffers(display, surface)) goto fail;
    glFinish();
    struct gbm_bo *input = gbm_surface_lock_front_buffer(gbmSurface);
    if (!input) goto fail;
    uint32_t inputWidth = gbm_bo_get_width(input);
    uint32_t inputHeight = gbm_bo_get_height(input);
    uint32_t inputStride = gbm_bo_get_stride(input);
    if (microFxIpu.converter.fd < 0 &&
        MicroFxIpuInitializeConverter(inputWidth, inputHeight, inputStride) != 0) {
        gbm_surface_release_buffer(gbmSurface, input);
        goto fail;
    }
    int inputDmaBufFd = gbm_bo_get_fd(input);
    uint32_t nextNativeIndex = microFxIpu.displayConfigured
                             ? (microFxIpu.currentNativeIndex + 1u) % 2u : 0u;
    double convertStart = MicroFxIpuClockMs();
    int convertResult = inputDmaBufFd < 0 ? -1 :
        MicroFxIpuConvert(inputDmaBufFd,
                          microFxIpu.nativeFrames[nextNativeIndex].dmaBufFd);
    if (inputDmaBufFd >= 0) close(inputDmaBufFd);
    double convertEnd = MicroFxIpuClockMs();
    gbm_surface_release_buffer(gbmSurface, input);
    if (convertResult != 0) goto fail;

    int result = 0;
    if (!microFxIpu.displayConfigured) {
        result = MicroFxIpuAtomicConfigure(
            crtc, connector, mode,
            microFxIpu.nativeFrames[nextNativeIndex].fbId, true);
        if (result == 0) {
            result = MicroFxIpuAtomicConfigure(
                crtc, connector, mode,
                microFxIpu.nativeFrames[nextNativeIndex].fbId, false);
        }
        if (result == 0) microFxIpu.displayConfigured = true;
    } else {
        result = MicroFxIpuAtomicPresent(
            microFxIpu.nativeFrames[nextNativeIndex].fbId);
    }
    if (result != 0) goto fail;
    microFxIpu.currentNativeIndex = nextNativeIndex;
    double conversion = convertEnd - convertStart;
    microFxIpu.convertTotalMs += conversion;
    if (conversion > microFxIpu.convertMaximumMs) {
        microFxIpu.convertMaximumMs = conversion;
    }
    microFxIpu.flipTotalMs += MicroFxIpuClockMs() - convertEnd;
    microFxIpu.profileFrames++;
    if ((microFxIpu.profileFrames % 120u) == 0u &&
        getenv("MICROFX_PROFILE") &&
        strcmp(getenv("MICROFX_PROFILE"), "1") == 0) {
        fprintf(stderr, "IPU_TIMING frames=120 convert_avg=%.2f convert_max=%.2f"
                " atomic_commit_avg=%.2f\n",
                microFxIpu.convertTotalMs/120.0,
                microFxIpu.convertMaximumMs,
                microFxIpu.flipTotalMs/120.0);
        microFxIpu.convertTotalMs = 0.0;
        microFxIpu.convertMaximumMs = 0.0;
        microFxIpu.flipTotalMs = 0.0;
    }
    return 0;

fail:
    fprintf(stderr, "MICROFX_IPU fatal stage=present errno=%d message=%s\n",
            errno, strerror(errno));
    fflush(stderr);
    microFxIpu.failed = true;
    return -1;
}

static int MicroFxIpuResizeSurface(int width, int height,
                                   struct gbm_device *device,
                                   struct gbm_surface **gbmSurface,
                                   EGLDisplay display, EGLConfig config,
                                   EGLContext context, EGLSurface *surface)
{
    if (!microFxIpu.initialized || width < 64 || height < 64) return -1;
    MicroFxIpuDestroyConverter();
    if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) return -1;
    if (*surface != EGL_NO_SURFACE) eglDestroySurface(display, *surface);
    if (*gbmSurface) gbm_surface_destroy(*gbmSurface);
    *surface = EGL_NO_SURFACE;
    *gbmSurface = gbm_surface_create(
        device, (uint32_t)width, (uint32_t)height, GBM_FORMAT_XRGB8888,
        GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    if (!*gbmSurface) return -1;
    *surface = eglCreateWindowSurface(display, config,
        (EGLNativeWindowType)*gbmSurface, NULL);
    if (*surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, *surface, *surface, context)) return -1;
    eglSwapInterval(display, 0);
    printf("MICROFX_IPU resize source=%dx%d output=%ux%u old_frame_held=yes modeset=no\n",
           width, height, microFxIpu.outputWidth, microFxIpu.outputHeight);
    fflush(stdout);
    return 0;
}

static void MicroFxIpuShutdown(void)
{
    if (!microFxIpu.requested) return;
    MicroFxIpuDestroyConverter();
    MicroFxIpuDestroyNativeFrame(&microFxIpu.nativeFrames[0]);
    MicroFxIpuDestroyNativeFrame(&microFxIpu.nativeFrames[1]);
    memset(&microFxIpu, 0, sizeof(microFxIpu));
    microFxIpu.drmFd = -1;
    microFxIpu.converter.fd = -1;
    microFxIpu.nativeFrames[0].dmaBufFd = -1;
    microFxIpu.nativeFrames[1].dmaBufFd = -1;
}
