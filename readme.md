# StreamCast 🎬

A lightweight local network video streaming server written in **pure C** — no Node.js, no Python, no frameworks. Uses HTTP/1.1 Range Requests so videos are seekable in the browser just like a real streaming service.

---

## 📁 Folder Structure

```
Project/
├── stream_server.c      # The entire server — one file, pure C
├── Makefile             # Build helper
└── videos/              # Put your video files here
    ├── movie1.mp4
    ├── clip2.webm
    └── ...
```

---

## ▶️ How to Run on Linux

### 1. Clone the repository

```bash
git clone https://github.com/<your-username>/<your-repo>.git
cd <your-repo>
```

### 2. Build the server

```bash
make
```

Or manually:

```bash
gcc -O2 -o stream_server stream_server.c -lpthread
```

### 3. Add your videos

Drop `.mp4`, `.webm`, `.mkv`, `.avi`, or `.mov` files into the `videos/` folder:

```bash
cp ~/Videos/movie.mp4 videos/
```

### 4. Start the server

```bash
./stream_server ./videos 8080
```

You'll see:

```
  ╔══════════════════════════════════════╗
  ║      StreamCast — Video Server       ║
  ╚══════════════════════════════════════╝

  Videos dir : ./videos
  Port       : 8080

  Open in browser:
    → http://localhost:8080
    → http://<your-hotspot-ip>:8080  (for other devices)
```

### 5. Open in browser

- **Same machine:** http://localhost:8080  
- **Other devices on the same hotspot/WiFi:** `http://<your-ip>:8080`

---

## 🌐 Finding Your Hotspot IP

```bash
ip addr show | grep "inet " | grep -v 127.0.0.1
```

Look for something like `192.168.43.1` — share that address with other devices on the same network.

---

## 🎛️ Usage

```
./stream_server <videos_folder> <port>
```

| Argument | Default | Description |
|---|---|---|
| `videos_folder` | `./videos` | Path to the folder containing your video files |
| `port` | `8080` | Port to listen on |

**Examples:**

```bash
./stream_server ./videos 8080          # default
./stream_server /home/user/Movies 9090 # custom path and port
```

---

## 📡 Supported Video Formats

| Format | MIME Type |
|--------|-----------|
| `.mp4` | `video/mp4` |
| `.webm` | `video/webm` |
| `.mkv` | `video/x-matroska` |
| `.ogg` / `.ogv` | `video/ogg` |
| `.avi` | `video/x-msvideo` |
| `.mov` | `video/quicktime` |

> **Note:** Browser-native playback works best with `.mp4` (H.264) and `.webm`. For `.mkv` and `.avi`, use a browser with broad codec support or convert files with `ffmpeg`.

---

## ⚙️ How It Works

```
Browser                        Server (C)
  │                               │
  │  GET /                        │
  │──────────────────────────────▶│  serve embedded HTML UI
  │◀──────────────────────────────│
  │                               │
  │  GET /api/videos              │
  │──────────────────────────────▶│  scan videos/ → return JSON list
  │◀──────────────────────────────│
  │                               │
  │  GET /videos/movie.mp4        │
  │──────────────────────────────▶│  open file, stream from byte 0
  │◀─────── 200 OK ───────────────│
  │                               │
  │  [user seeks to 2:30]         │
  │  Range: bytes=9437184-        │
  │──────────────────────────────▶│  lseek() to that offset, stream
  │◀─────── 206 Partial Content ──│
```

Key implementation details:
- **HTTP Range Requests** (RFC 7233) — enables seeking without downloading the whole file
- **One thread per client** via `pthreads` — multiple devices can stream simultaneously
- **HTML UI embedded in the binary** — no separate web files needed
- **Path traversal protection** — filenames with `..` or `/` are rejected

---

## 🛠️ Requirements

- GCC (any modern version)
- Linux (Ubuntu, Debian, Fedora, Arch, etc.)
- `pthreads` — included in standard glibc, no extra install needed

---
