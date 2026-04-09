#include "loopback.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

void *loopback_thread(void *arg) {
    loopback_args_t *a = arg;

    /* O_NONBLOCK: write() returns EAGAIN when no readers are attached */
    int fd = open(a->dev_path, O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("loopback: open"); return NULL; }

    /* Set format: MJPEG 1920x1080.
     * sizeimage is an upper bound; v4l2loopback uses the actual write() size. */
    struct v4l2_format fmt = {0};
    fmt.type                 = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width        = 1920;
    fmt.fmt.pix.height       = 1080;
    fmt.fmt.pix.pixelformat  = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field        = V4L2_FIELD_NONE;
    fmt.fmt.pix.sizeimage    = 1920 * 1080 * 2;
    fmt.fmt.pix.bytesperline = 0;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("loopback: VIDIOC_S_FMT"); goto err;
    }

    uint8_t *frame_copy = NULL;
    size_t   frame_cap  = 0;

    /* ~30fps loop */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 33333333 };

    for (;;) {
        nanosleep(&ts, NULL);

        size_t len = frame_buf_read(a->fb, &frame_copy, &frame_cap);
        if (len == 0) continue;

        ssize_t w = write(fd, frame_copy, len);
        if (w < 0 && errno != EAGAIN) {
            /* EAGAIN = no readers, silent skip. Anything else is worth logging. */
            perror("loopback: write");
        }
    }

    free(frame_copy);
err:
    close(fd);
    return NULL;
}
