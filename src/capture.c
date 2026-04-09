#include "capture.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define NUM_BUFS 4

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

void *capture_thread(void *arg) {
    capture_args_t *a = arg;
    int fd = open(a->dev_path, O_RDWR | O_NONBLOCK);
    if (fd < 0) { perror("capture: open"); return NULL; }

    /* Set format: MJPEG 1920x1080 */
    struct v4l2_format fmt = {0};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = 1920;
    fmt.fmt.pix.height      = 1080;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("capture: VIDIOC_S_FMT"); goto err_close;
    }

    /* Set framerate: 1/30 */
    struct v4l2_streamparm parm = {0};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = 30;
    /* best-effort; not fatal if camera ignores it */
    xioctl(fd, VIDIOC_S_PARM, &parm);

    /* Request MMAP buffers */
    struct v4l2_requestbuffers req = {0};
    req.count  = NUM_BUFS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("capture: VIDIOC_REQBUFS"); goto err_close;
    }

    /* Map buffers */
    void   *bufs[NUM_BUFS];
    size_t  lens[NUM_BUFS];
    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("capture: VIDIOC_QUERYBUF"); goto err_close;
        }
        lens[i] = buf.length;
        bufs[i] = mmap(NULL, buf.length, PROT_READ, MAP_SHARED, fd, buf.m.offset);
        if (bufs[i] == MAP_FAILED) { perror("capture: mmap"); goto err_close; }

        struct v4l2_buffer qbuf = {0};
        qbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        qbuf.memory = V4L2_MEMORY_MMAP;
        qbuf.index  = i;
        if (xioctl(fd, VIDIOC_QBUF, &qbuf) < 0) {
            perror("capture: VIDIOC_QBUF"); goto err_unmap;
        }
    }

    /* Stream on */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("capture: VIDIOC_STREAMON"); goto err_unmap;
    }

    /* Capture loop */
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    for (;;) {
        int r = poll(&pfd, 1, 2000);
        if (r < 0) { perror("capture: poll"); break; }
        if (r == 0) { fprintf(stderr, "capture: poll timeout\n"); continue; }

        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("capture: VIDIOC_DQBUF"); break;
        }

        frame_buf_update(a->fb, bufs[buf.index], buf.bytesused);

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("capture: VIDIOC_QBUF"); break;
        }
    }

    xioctl(fd, VIDIOC_STREAMOFF, &type);
err_unmap:
    for (unsigned i = 0; i < req.count; i++)
        if (bufs[i] != MAP_FAILED) munmap(bufs[i], lens[i]);
err_close:
    close(fd);
    return NULL;
}
