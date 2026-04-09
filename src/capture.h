#pragma once
#include "shared.h"

typedef struct {
    const char  *dev_path;
    frame_buf_t *fb;
} capture_args_t;

void *capture_thread(void *arg);
