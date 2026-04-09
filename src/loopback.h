#pragma once
#include "shared.h"

typedef struct {
    const char  *dev_path;
    frame_buf_t *fb;
} loopback_args_t;

void *loopback_thread(void *arg);
