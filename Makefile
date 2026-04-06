# StreamCast Makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11
LDFLAGS = -lpthread
TARGET  = stream_server

all: $(TARGET)

$(TARGET): stream_server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo ""
	@echo "  ✓ Build successful → ./$(TARGET)"
	@echo "  Usage: ./$(TARGET) <videos_dir> [port]"
	@echo "  Example: ./$(TARGET) ./videos 8080"
	@echo ""

clean:
	rm -f $(TARGET)

# Create a test videos directory
setup:
	mkdir -p videos
	@echo "  Put your .mp4 / .webm / .mkv files in ./videos/"
	@echo "  Then run: ./$(TARGET) ./videos 8080"

.PHONY: all clean setup
