#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pthread_rwlock_t lock;
    uint8_t         *buf;
    size_t           len;
    size_t           cap;
} frame_buf_t;

static inline void frame_buf_init(frame_buf_t *fb) {
    pthread_rwlock_init(&fb->lock, NULL);
    fb->buf = NULL;
    fb->len = 0;
    fb->cap = 0;
}

/* Called from capture thread only. Grows buffer if needed, copies frame in. */
static inline void frame_buf_update(frame_buf_t *fb, const uint8_t *data, size_t len) {
    pthread_rwlock_wrlock(&fb->lock);
    if (len > fb->cap) {
        free(fb->buf);
        fb->buf = malloc(len);
        fb->cap = len;
    }
    memcpy(fb->buf, data, len);
    fb->len = len;
    pthread_rwlock_unlock(&fb->lock);
}

/*
 * Copy current frame into *out (caller-allocated or NULL).
 * If *out is NULL or *cap < frame len, reallocs *out.
 * Returns copied length, or 0 if no frame yet.
 */
static inline size_t frame_buf_read(frame_buf_t *fb, uint8_t **out, size_t *cap) {
    pthread_rwlock_rdlock(&fb->lock);
    size_t len = fb->len;
    if (len == 0) {
        pthread_rwlock_unlock(&fb->lock);
        return 0;
    }
    if (*cap < len) {
        free(*out);
        *out = malloc(len);
        *cap = len;
    }
    memcpy(*out, fb->buf, len);
    pthread_rwlock_unlock(&fb->lock);
    return len;
}
