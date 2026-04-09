#include "http.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define RECV_BUF 4096

static void handle_conn(int cfd, frame_buf_t *fb) {
    /* Read request — we only need to drain headers; we always serve /snapshot */
    char req[RECV_BUF];
    ssize_t n = recv(cfd, req, sizeof(req) - 1, 0);
    if (n <= 0) return;
    req[n] = '\0';

    /* Minimal validation: must be GET */
    if (strncmp(req, "GET ", 4) != 0) {
        const char *bad = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
        send(cfd, bad, strlen(bad), MSG_NOSIGNAL);
        return;
    }

    /* Copy current frame */
    uint8_t *frame = NULL;
    size_t   cap   = 0;
    size_t   len   = frame_buf_read(fb, &frame, &cap);

    if (len == 0) {
        const char *none = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";
        send(cfd, none, strlen(none), MSG_NOSIGNAL);
        free(frame);
        return;
    }

    /* Send headers */
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        len);
    send(cfd, hdr, (size_t)hlen, MSG_NOSIGNAL);

    /* Send frame */
    size_t sent = 0;
    while (sent < len) {
        ssize_t w = send(cfd, frame + sent, len - sent, MSG_NOSIGNAL);
        if (w <= 0) break;
        sent += (size_t)w;
    }

    free(frame);
}

void http_serve(int port, frame_buf_t *fb) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("http: socket"); return; }

    int one = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("http: bind"); close(sfd); return;
    }
    if (listen(sfd, 16) < 0) {
        perror("http: listen"); close(sfd); return;
    }

    fprintf(stderr, "http: listening on port %d\n", port);

    for (;;) {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) { perror("http: accept"); continue; }
        handle_conn(cfd, fb);
        close(cfd);
    }

    close(sfd);
}
