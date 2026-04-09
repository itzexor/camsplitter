# camsplitter

> Used as part of my PiK1 setup. This is to allow MJPEG frame capture for snapshots with moonraker when using mediamtx with on demand ffmpeg transcoding. This allows me to take advantage of being able to explicitly specify MJPEG -> YUV -> Pi HW Encoder pipeline for maximum camera performance (Creality K1 cam), while still using live snapshots.
>
> MediaMTX's solution is to periodically capture a frame from a standard output, which means running the on demand transcoder basically 24/7 and having a pretty out of date snapshot (not great for timelapsing).
>
> This solution is much more lightweight, original quality, and realtime(ish).
>
> It's also very specifically tailored to my exact usage, and jankily vibecoded.

A lightweight Linux daemon that reads a single MJPEG webcam and fans it out to two consumers simultaneously:

- **v4l2loopback** — a virtual `/dev/videoN` device that any V4L2 application (OBS, ffmpeg, browsers, etc.) can open as a normal camera
- **HTTP snapshot endpoint** — serves the latest JPEG frame on demand over HTTP

This solves the common problem of a webcam that can only be opened by one process at a time.

## How it works

```
/dev/video0 (real camera)
      │
      │  V4L2 MMAP capture @ 1920×1080 MJPEG 30fps
      ▼
 capture thread  ──►  shared frame buffer (rwlock)
                              │
              ┌───────────────┴──────────────┐
              ▼                              ▼
      loopback thread                  http_serve()
   writes to /dev/video50          GET any path → JPEG
   (v4l2loopback device)           HTTP/1.1, one frame per request
```

The shared frame buffer is protected by a `pthread_rwlock_t`: the capture thread holds a write lock while copying a new frame in; the loopback thread and HTTP handler hold read locks while copying a frame out.

## Dependencies

| Dependency | Purpose |
|---|---|
| `v4l2loopback-dkms` | Kernel module that creates the virtual camera device |
| `gcc`, `make` | Build toolchain |
| Linux kernel headers | Provided by your distro's kernel headers package |

Install on Arch Linux:
```sh
sudo pacman -S v4l2loopback-dkms linux-headers
```

Install on Debian/Ubuntu:
```sh
sudo apt install v4l2loopback-dkms linux-headers-$(uname -r)
```

## Build

```sh
make
```

This produces the `camsplitter` binary in the project root.

```sh
make clean   # remove the binary
```

## Usage

```
camsplitter -c <camera> [-n <video_nr>] [-p <port>]

  -c /dev/video0   camera device (required; must support MJPEG 1920x1080)
  -n 50            v4l2loopback device number (default: 50 → /dev/video50)
  -p 8080          HTTP snapshot port (default: 8080)
```

Example:
```sh
./camsplitter -c /dev/video0 -n 50 -p 8080
```

On startup, `camsplitter` loads `v4l2loopback` via `modprobe` (requires `CAP_SYS_MODULE` or root). On exit (SIGINT/SIGTERM), it unloads the module via `modprobe -r`.

### HTTP snapshot

Any GET request to the HTTP port returns the latest JPEG frame:

```sh
curl http://localhost:8080/ --output snapshot.jpg
# or open http://localhost:8080/ in a browser
```

Returns `503 Service Unavailable` if no frame has been captured yet.

### Virtual camera

The loopback device (e.g. `/dev/video50`) appears as a normal V4L2 camera. Open it with any application:

```sh
ffplay /dev/video50
mpv av://v4l2:/dev/video50
```

## Running as a systemd service

An example unit file is provided at [camsplitter.service](camsplitter.service). It is tailored for a specific setup and is not intended as a universal install guide — adapt it to your own service manager configuration as needed.

## Notes

- The camera must support **MJPEG at 1920×1080**. If your camera does not, adjust the format in [src/capture.c](src/capture.c) and [src/loopback.c](src/loopback.c).
- The loopback thread writes at ~30fps. If no application has the loopback device open, writes return `EAGAIN` and are silently skipped.
- The HTTP server is single-threaded (one connection at a time). It is intended for periodic snapshots, not streaming.
