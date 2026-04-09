#pragma once
#include "shared.h"

/* Blocks forever serving GET /snapshot on the given port. */
void http_serve(int port, frame_buf_t *fb);
