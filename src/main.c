#include "capture.h"
#include "http.h"
#include "loopback.h"
#include "shared.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void teardown(void) {
    fprintf(stderr, "camsplitter: removing v4l2loopback\n");
    system("modprobe -r v4l2loopback");
}

static void sig_handler(int sig) {
    (void)sig;
    teardown();
    _exit(0);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -c <camera> [-n <video_nr>] [-p <port>]\n"
        "  -c /dev/video0  camera device (must support MJPEG 1920x1080)\n"
        "  -n 50           v4l2loopback video number (default: 50)\n"
        "  -p 8080         HTTP snapshot port (default: 8080)\n",
        prog);
}

int main(int argc, char **argv) {
    const char *camera_dev = NULL;
    int loopback_nr = 50;
    int port = 8080;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            camera_dev = argv[++i];
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            loopback_nr = atoi(argv[++i]);
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);
        else { usage(argv[0]); return 1; }
    }

    if (!camera_dev) { usage(argv[0]); return 1; }

    /* Load v4l2loopback */
    char modprobe_cmd[256];
    snprintf(modprobe_cmd, sizeof(modprobe_cmd),
        "modprobe v4l2loopback video_nr=%d card_label=camsplitter exclusive_caps=1",
        loopback_nr);
    fprintf(stderr, "camsplitter: %s\n", modprobe_cmd);
    if (system(modprobe_cmd) != 0)
        fprintf(stderr, "camsplitter: modprobe failed — module may already be loaded, continuing\n");

    /* Verify the loopback device exists */
    char loopback_dev[32];
    snprintf(loopback_dev, sizeof(loopback_dev), "/dev/video%d", loopback_nr);
    {
        struct stat st;
        if (stat(loopback_dev, &st) != 0 || !S_ISCHR(st.st_mode)) {
            fprintf(stderr, "camsplitter: %s not found after modprobe: %s\n"
                            "  Is v4l2loopback-dkms installed and built for the running kernel?\n",
                    loopback_dev, strerror(errno));
            return 1;
        }
    }

    fprintf(stderr, "camsplitter: loopback device: %s\n", loopback_dev);

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    atexit(teardown);

    frame_buf_t fb;
    frame_buf_init(&fb);

    capture_args_t  cap_args  = { .dev_path = camera_dev,   .fb = &fb };
    loopback_args_t loop_args = { .dev_path = loopback_dev, .fb = &fb };

    pthread_t cap_tid, loop_tid;
    pthread_create(&cap_tid,  NULL, capture_thread,  &cap_args);
    pthread_create(&loop_tid, NULL, loopback_thread, &loop_args);

    http_serve(port, &fb);

    pthread_join(cap_tid,  NULL);
    pthread_join(loop_tid, NULL);
    return 0;
}
