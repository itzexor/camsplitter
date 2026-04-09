CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -pthread -I src
LDFLAGS = -pthread
SRCS    = src/main.c src/capture.c src/loopback.c src/http.c
TARGET  = camsplitter

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
