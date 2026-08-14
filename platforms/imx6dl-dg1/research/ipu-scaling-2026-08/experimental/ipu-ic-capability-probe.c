#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int IoctlRetry(int fd, unsigned long request, void *argument)
{
    int result;
    do result = ioctl(fd, request, argument); while (result < 0 && errno == EINTR);
    return result;
}

static void FourccString(uint32_t fourcc, char out[5])
{
    out[0] = (char)(fourcc & 0xffu);
    out[1] = (char)((fourcc >> 8) & 0xffu);
    out[2] = (char)((fourcc >> 16) & 0xffu);
    out[3] = (char)((fourcc >> 24) & 0xffu);
    out[4] = '\0';
}

static void EnumerateFormats(int fd, enum v4l2_buf_type type, const char *queue)
{
    for (uint32_t index = 0;; index++) {
        struct v4l2_fmtdesc description = {
            .index = index,
            .type = type
        };
        if (IoctlRetry(fd, VIDIOC_ENUM_FMT, &description) != 0) {
            if (errno != EINVAL) {
                printf("FORMAT_ERROR queue=%s index=%" PRIu32 " errno=%d message=%s\n",
                       queue, index, errno, strerror(errno));
            }
            break;
        }
        char format[5];
        FourccString(description.pixelformat, format);
        printf("FORMAT queue=%s index=%" PRIu32 " fourcc=%s description=%s flags=0x%08" PRIx32 "\n",
               queue, index, format, description.description, description.flags);
    }
}

static int SetFormat(int fd, enum v4l2_buf_type type, const char *queue,
                     uint32_t width, uint32_t height, uint32_t pixelFormat,
                     struct v4l2_format *negotiated)
{
    memset(negotiated, 0, sizeof(*negotiated));
    negotiated->type = type;
    negotiated->fmt.pix.width = width;
    negotiated->fmt.pix.height = height;
    negotiated->fmt.pix.pixelformat = pixelFormat;
    negotiated->fmt.pix.field = V4L2_FIELD_NONE;
    negotiated->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    if (IoctlRetry(fd, VIDIOC_S_FMT, negotiated) != 0) {
        printf("NEGOTIATE queue=%s supported=no errno=%d message=%s\n",
               queue, errno, strerror(errno));
        return -1;
    }
    char format[5];
    FourccString(negotiated->fmt.pix.pixelformat, format);
    printf("NEGOTIATE queue=%s supported=yes fourcc=%s width=%" PRIu32
           " height=%" PRIu32 " bytesPerLine=%" PRIu32 " sizeImage=%" PRIu32 "\n",
           queue, format,
           negotiated->fmt.pix.width,
           negotiated->fmt.pix.height,
           negotiated->fmt.pix.bytesperline,
           negotiated->fmt.pix.sizeimage);
    return 0;
}

static int ProbeDmaBufQueue(int fd, enum v4l2_buf_type type, const char *queue)
{
    struct v4l2_requestbuffers request = {
        .count = 0,
        .type = type,
        .memory = V4L2_MEMORY_DMABUF
    };
    int result = IoctlRetry(fd, VIDIOC_REQBUFS, &request);
    printf("DMABUF_QUEUE queue=%s supported=%s errno=%d\n",
           queue, result == 0 ? "yes" : "no", result == 0 ? 0 : errno);
    return result;
}

int main(int argc, char **argv)
{
    const char *device = argc > 1 ? argv[1] : "/dev/video4";
    uint32_t inputWidth = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 960;
    uint32_t inputHeight = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 540;
    uint32_t outputWidth = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) : 1920;
    uint32_t outputHeight = argc > 5 ? (uint32_t)strtoul(argv[5], NULL, 10) : 1080;

    int fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "ERROR open device=%s errno=%d message=%s\n",
                device, errno, strerror(errno));
        return 1;
    }
    struct v4l2_capability capability;
    memset(&capability, 0, sizeof(capability));
    if (IoctlRetry(fd, VIDIOC_QUERYCAP, &capability) != 0) {
        close(fd);
        return 1;
    }
    uint32_t effective = (capability.capabilities & V4L2_CAP_DEVICE_CAPS) ?
        capability.device_caps : capability.capabilities;
    printf("MICROFX_IPU_IC_PROBE schema=1 device=%s driver=%s card=%s bus=%s"
           " capabilities=0x%08" PRIx32 " deviceCaps=0x%08" PRIx32 "\n",
           device, capability.driver, capability.card, capability.bus_info,
           capability.capabilities, effective);
    printf("CAP name=videoM2M supported=%s\n",
           (effective & V4L2_CAP_VIDEO_M2M) ? "yes" : "no");
    printf("CAP name=streaming supported=%s\n",
           (effective & V4L2_CAP_STREAMING) ? "yes" : "no");
    EnumerateFormats(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT, "output");
    EnumerateFormats(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, "capture");

    struct v4l2_format input;
    struct v4l2_format output;
    int inputResult = SetFormat(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT, "output",
                                inputWidth, inputHeight, V4L2_PIX_FMT_XBGR32, &input);
    int outputResult = SetFormat(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, "capture",
                                 outputWidth, outputHeight, V4L2_PIX_FMT_XBGR32, &output);
    int inputDma = ProbeDmaBufQueue(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT, "output");
    int outputDma = ProbeDmaBufQueue(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, "capture");
    bool exact = inputResult == 0 && outputResult == 0 &&
                 input.fmt.pix.width == inputWidth && input.fmt.pix.height == inputHeight &&
                 output.fmt.pix.width == outputWidth && output.fmt.pix.height == outputHeight &&
                 input.fmt.pix.pixelformat == V4L2_PIX_FMT_XBGR32 &&
                 output.fmt.pix.pixelformat == V4L2_PIX_FMT_XBGR32;
    printf("MICROFX_EVIDENCE xrgbScaleNegotiation=%s\n", exact ? "yes" : "no");
    printf("MICROFX_EVIDENCE v4l2DmaBufQueues=%s\n",
           inputDma == 0 && outputDma == 0 ? "yes" : "no");
    close(fd);
    return exact && inputDma == 0 && outputDma == 0 ? 0 : 1;
}
