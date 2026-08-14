#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

static const char *PlaneTypeName(uint64_t value)
{
    switch (value) {
        case DRM_PLANE_TYPE_OVERLAY: return "overlay";
        case DRM_PLANE_TYPE_PRIMARY: return "primary";
        case DRM_PLANE_TYPE_CURSOR: return "cursor";
        default: return "unknown";
    }
}

static void FourccString(uint32_t format, char out[5])
{
    out[0] = (char)(format & 0xffu);
    out[1] = (char)((format >> 8) & 0xffu);
    out[2] = (char)((format >> 16) & 0xffu);
    out[3] = (char)((format >> 24) & 0xffu);
    out[4] = '\0';
    for (int index = 0; index < 4; index++) {
        if (out[index] < 32 || out[index] > 126) out[index] = '?';
    }
}

static uint64_t PropertyValue(const drmModeObjectProperties *properties,
                              uint32_t propertyId)
{
    for (uint32_t index = 0; index < properties->count_props; index++) {
        if (properties->props[index] == propertyId) {
            return properties->prop_values[index];
        }
    }
    return 0;
}

static void PrintProperty(int fd, uint32_t planeId,
                          const drmModeObjectProperties *properties,
                          uint32_t propertyId)
{
    drmModePropertyRes *property = drmModeGetProperty(fd, propertyId);
    if (!property) return;

    uint64_t value = PropertyValue(properties, propertyId);
    printf("PROPERTY plane=%" PRIu32 " id=%" PRIu32 " name=%s value=%" PRIu64
           " flags=0x%08" PRIx32,
           planeId, propertyId, property->name, value, property->flags);

    if ((property->flags & DRM_MODE_PROP_RANGE) && property->count_values >= 2) {
        printf(" range=%" PRIu64 ":%" PRIu64,
               property->values[0], property->values[1]);
    }
#ifdef DRM_MODE_PROP_SIGNED_RANGE
    if ((property->flags & DRM_MODE_PROP_SIGNED_RANGE) && property->count_values >= 2) {
        printf(" signed_range=%" PRId64 ":%" PRId64,
               (int64_t)property->values[0], (int64_t)property->values[1]);
    }
#endif
    if (property->flags & (DRM_MODE_PROP_ENUM | DRM_MODE_PROP_BITMASK)) {
        printf(" enums=");
        for (int index = 0; index < property->count_enums; index++) {
            if (index > 0) putchar(',');
            printf("%s:%" PRIu64,
                   property->enums[index].name,
                   property->enums[index].value);
        }
    }
    putchar('\n');
    drmModeFreeProperty(property);
}

static const char *ReadPlaneType(int fd,
                                 const drmModeObjectProperties *properties,
                                 uint64_t *rawType)
{
    for (uint32_t index = 0; index < properties->count_props; index++) {
        drmModePropertyRes *property = drmModeGetProperty(fd, properties->props[index]);
        if (!property) continue;
        if (strcmp(property->name, "type") == 0) {
            *rawType = properties->prop_values[index];
            drmModeFreeProperty(property);
            return PlaneTypeName(*rawType);
        }
        drmModeFreeProperty(property);
    }
    *rawType = UINT64_MAX;
    return "unknown";
}

static void PrintCapabilities(int fd)
{
    struct {
        uint64_t id;
        const char *name;
    } capabilities[] = {
        { DRM_CAP_DUMB_BUFFER, "dumbBuffer" },
        { DRM_CAP_PRIME, "prime" },
        { DRM_CAP_ADDFB2_MODIFIERS, "addfb2Modifiers" },
        { DRM_CAP_TIMESTAMP_MONOTONIC, "timestampMonotonic" },
#ifdef DRM_CAP_SYNCOBJ
        { DRM_CAP_SYNCOBJ, "syncobj" },
#endif
#ifdef DRM_CAP_SYNCOBJ_TIMELINE
        { DRM_CAP_SYNCOBJ_TIMELINE, "syncobjTimeline" },
#endif
    };

    for (size_t index = 0; index < sizeof(capabilities) / sizeof(capabilities[0]); index++) {
        uint64_t value = 0;
        int result = drmGetCap(fd, capabilities[index].id, &value);
        printf("CAP name=%s supported=%s value=%" PRIu64 " errno=%d\n",
               capabilities[index].name,
               result == 0 ? "yes" : "no",
               value,
               result == 0 ? 0 : errno);
    }
}

static const char *ConnectionName(drmModeConnection connection)
{
    switch (connection) {
        case DRM_MODE_CONNECTED: return "connected";
        case DRM_MODE_DISCONNECTED: return "disconnected";
        case DRM_MODE_UNKNOWNCONNECTION: return "unknown";
        default: return "invalid";
    }
}

static void PrintDisplayResources(int fd, const drmModeRes *resources)
{
    for (int index = 0; index < resources->count_crtcs; index++) {
        drmModeCrtc *crtc = drmModeGetCrtc(fd, resources->crtcs[index]);
        if (!crtc) continue;
        printf("CRTC index=%d id=%" PRIu32 " fb=%" PRIu32
               " x=%" PRIu32 " y=%" PRIu32 " modeValid=%d",
               index, crtc->crtc_id, crtc->buffer_id,
               crtc->x, crtc->y, crtc->mode_valid);
        if (crtc->mode_valid) {
            printf(" mode=%s width=%u height=%u refresh=%u clockKHz=%u",
                   crtc->mode.name,
                   crtc->mode.hdisplay,
                   crtc->mode.vdisplay,
                   crtc->mode.vrefresh,
                   crtc->mode.clock);
        }
        putchar('\n');
        drmModeFreeCrtc(crtc);
    }

    for (int index = 0; index < resources->count_connectors; index++) {
        drmModeConnector *connector = drmModeGetConnector(fd, resources->connectors[index]);
        if (!connector) continue;
        printf("CONNECTOR id=%" PRIu32 " connection=%s encoder=%" PRIu32
               " modes=%d mm=%ux%u\n",
               connector->connector_id,
               ConnectionName(connector->connection),
               connector->encoder_id,
               connector->count_modes,
               connector->mmWidth,
               connector->mmHeight);
        for (int modeIndex = 0; modeIndex < connector->count_modes; modeIndex++) {
            const drmModeModeInfo *mode = &connector->modes[modeIndex];
            printf("MODE connector=%" PRIu32 " index=%d name=%s width=%u height=%u"
                   " refresh=%u clockKHz=%u preferred=%s type=0x%08" PRIx32
                   " flags=0x%08" PRIx32 "\n",
                   connector->connector_id,
                   modeIndex,
                   mode->name,
                   mode->hdisplay,
                   mode->vdisplay,
                   mode->vrefresh,
                   mode->clock,
                   (mode->type & DRM_MODE_TYPE_PREFERRED) ? "yes" : "no",
                   mode->type,
                   mode->flags);
        }
        drmModeFreeConnector(connector);
    }
}

int main(int argc, char **argv)
{
    const char *device = argc > 1 ? argv[1] : "/dev/dri/card1";
    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "ERROR open device=%s errno=%d message=%s\n",
                device, errno, strerror(errno));
        return 1;
    }

    drmVersionPtr version = drmGetVersion(fd);
    printf("MICROFX_KMS_PROBE schema=1 device=%s driver=%s version=%d.%d.%d\n",
           device,
           version && version->name ? version->name : "unknown",
           version ? version->version_major : 0,
           version ? version->version_minor : 0,
           version ? version->version_patchlevel : 0);
    if (version) drmFreeVersion(version);

    int universalResult = drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    int universalError = universalResult == 0 ? 0 : errno;
    int atomicResult = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
    int atomicError = atomicResult == 0 ? 0 : errno;
    printf("CLIENT_CAP name=universalPlanes supported=%s errno=%d\n",
           universalResult == 0 ? "yes" : "no", universalError);
    printf("CLIENT_CAP name=atomic supported=%s errno=%d\n",
           atomicResult == 0 ? "yes" : "no", atomicError);
    PrintCapabilities(fd);

    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources) {
        fprintf(stderr, "ERROR resources errno=%d message=%s\n", errno, strerror(errno));
        close(fd);
        return 1;
    }
    printf("RESOURCES crtcs=%d connectors=%d encoders=%d min=%dx%d max=%dx%d\n",
           resources->count_crtcs,
           resources->count_connectors,
           resources->count_encoders,
           resources->min_width,
           resources->min_height,
           resources->max_width,
           resources->max_height);
    PrintDisplayResources(fd, resources);

    drmModePlaneRes *planeResources = drmModeGetPlaneResources(fd);
    if (!planeResources) {
        fprintf(stderr, "ERROR planes errno=%d message=%s\n", errno, strerror(errno));
        drmModeFreeResources(resources);
        close(fd);
        return 1;
    }
    printf("PLANES count=%" PRIu32 "\n", planeResources->count_planes);

    for (uint32_t planeIndex = 0; planeIndex < planeResources->count_planes; planeIndex++) {
        uint32_t planeId = planeResources->planes[planeIndex];
        drmModePlane *plane = drmModeGetPlane(fd, planeId);
        drmModeObjectProperties *properties = drmModeObjectGetProperties(
            fd, planeId, DRM_MODE_OBJECT_PLANE);
        if (!plane || !properties) {
            printf("PLANE id=%" PRIu32 " available=no errno=%d\n", planeId, errno);
            if (plane) drmModeFreePlane(plane);
            if (properties) drmModeFreeObjectProperties(properties);
            continue;
        }

        uint64_t rawType = UINT64_MAX;
        const char *type = ReadPlaneType(fd, properties, &rawType);
        printf("PLANE id=%" PRIu32 " type=%s typeValue=%" PRIu64
               " crtc=%" PRIu32 " fb=%" PRIu32 " possibleCrtcs=0x%08" PRIx32
               " gamma=%" PRIu32 " formats=",
               plane->plane_id,
               type,
               rawType,
               plane->crtc_id,
               plane->fb_id,
               plane->possible_crtcs,
               plane->gamma_size);
        for (uint32_t formatIndex = 0; formatIndex < plane->count_formats; formatIndex++) {
            char formatName[5];
            FourccString(plane->formats[formatIndex], formatName);
            if (formatIndex > 0) putchar(',');
            printf("%s", formatName);
        }
        putchar('\n');

        for (uint32_t propertyIndex = 0;
             propertyIndex < properties->count_props;
             propertyIndex++) {
            PrintProperty(fd, planeId, properties, properties->props[propertyIndex]);
        }
        drmModeFreeObjectProperties(properties);
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(planeResources);
    drmModeFreeResources(resources);
    close(fd);
    return 0;
}
