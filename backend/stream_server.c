/*
 * StreamCast - Local Network Video Streaming Server
 * Compile: gcc -o stream_server stream_server.c -lpthread
 * Run:     ./stream_server ./videos 8080
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>

/* ─── Config ─────────────────────────────────────────────── */
#define MAX_PATH        512
#define BUFFER_SIZE     (64 * 1024)   /* 64 KB I/O buffer     */
#define SEND_CHUNK      (256 * 1024)  /* 256 KB streaming chunk*/
#define MAX_HEADERS     64
#define MAX_CLIENTS     64
#define DEFAULT_PORT    8080

/* ─── Globals ─────────────────────────────────────────────── */
static char videos_dir[MAX_PATH];
static int  server_fd;

/* ─── Logging ─────────────────────────────────────────────── */
static void log_msg(const char *level, const char *fmt, ...) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    printf("[%s] [%s] ", ts, level);
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n"); fflush(stdout);
}
#define LOG_INFO(...)  log_msg("INFO ", __VA_ARGS__)
#define LOG_WARN(...)  log_msg("WARN ", __VA_ARGS__)
#define LOG_ERROR(...) log_msg("ERROR", __VA_ARGS__)

/* ─── HTTP Helpers ─────────────────────────────────────────── */
typedef struct {
    int    fd;
    char   method[8];
    char   path[MAX_PATH];
    char   headers[MAX_HEADERS][2][256];
    int    header_count;
    long   range_start;
    long   range_end;       /* -1 = not set */
    int    has_range;
} Request;

/* URL decode in-place */
static void url_decode(char *dst, const char *src, size_t max) {
    size_t i = 0, j = 0;
    while (src[i] && j + 1 < max) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = { src[i+1], src[i+2], 0 };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' '; i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

/* Send full buffer */
static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

/* Send formatted header string */
static void send_fmt(int fd, const char *fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    send_all(fd, buf, n);
}

/* ─── MIME types ───────────────────────────────────────────── */
static const char *mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcasecmp(ext, ".mp4")  == 0) return "video/mp4";
    if (strcasecmp(ext, ".webm") == 0) return "video/webm";
    if (strcasecmp(ext, ".mkv")  == 0) return "video/x-matroska";
    if (strcasecmp(ext, ".ogg")  == 0) return "video/ogg";
    if (strcasecmp(ext, ".ogv")  == 0) return "video/ogg";
    if (strcasecmp(ext, ".avi")  == 0) return "video/x-msvideo";
    if (strcasecmp(ext, ".mov")  == 0) return "video/quicktime";
    if (strcasecmp(ext, ".html") == 0) return "text/html";
    if (strcasecmp(ext, ".json") == 0) return "application/json";
    return "application/octet-stream";
}

static int is_video(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".mp4")  == 0 ||
            strcasecmp(ext, ".webm") == 0 ||
            strcasecmp(ext, ".mkv")  == 0 ||
            strcasecmp(ext, ".ogg")  == 0 ||
            strcasecmp(ext, ".ogv")  == 0 ||
            strcasecmp(ext, ".avi")  == 0 ||
            strcasecmp(ext, ".mov")  == 0);
}

/* ─── Embedded HTML UI ─────────────────────────────────────── */
static const char HTML_UI[] =
"<!DOCTYPE html>\n"
"<html lang='en'>\n"
"<head>\n"
"<meta charset='UTF-8'>\n"
"<meta name='viewport' content='width=device-width, initial-scale=1'>\n"
"<title>StreamCast</title>\n"
"<style>\n"
"  @import url('https://fonts.googleapis.com/css2?family=Bebas+Neue&family=DM+Sans:wght@300;400;500&display=swap');\n"
"  :root {\n"
"    --bg: #0a0a0f;\n"
"    --surface: #111118;\n"
"    --card: #18181f;\n"
"    --border: #2a2a38;\n"
"    --accent: #e8365d;\n"
"    --accent2: #ff6b35;\n"
"    --text: #f0f0f8;\n"
"    --muted: #666680;\n"
"  }\n"
"  * { box-sizing: border-box; margin: 0; padding: 0; }\n"
"  body {\n"
"    background: var(--bg);\n"
"    color: var(--text);\n"
"    font-family: 'DM Sans', sans-serif;\n"
"    min-height: 100vh;\n"
"    overflow-x: hidden;\n"
"  }\n"
"  /* Noise texture overlay */\n"
"  body::before {\n"
"    content: '';\n"
"    position: fixed; inset: 0;\n"
"    background-image: url(\"data:image/svg+xml,%3Csvg viewBox='0 0 256 256' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='noise'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.9' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23noise)' opacity='0.04'/%3E%3C/svg%3E\");\n"
"    pointer-events: none; z-index: 999; opacity: 0.6;\n"
"  }\n"
"  header {\n"
"    padding: 28px 48px;\n"
"    border-bottom: 1px solid var(--border);\n"
"    display: flex; align-items: center; gap: 16px;\n"
"    position: sticky; top: 0;\n"
"    background: rgba(10,10,15,0.92);\n"
"    backdrop-filter: blur(12px);\n"
"    z-index: 100;\n"
"  }\n"
"  .logo {\n"
"    font-family: 'Bebas Neue', sans-serif;\n"
"    font-size: 2rem;\n"
"    letter-spacing: 3px;\n"
"    background: linear-gradient(135deg, var(--accent), var(--accent2));\n"
"    -webkit-background-clip: text;\n"
"    -webkit-text-fill-color: transparent;\n"
"    background-clip: text;\n"
"  }\n"
"  .logo-dot { color: var(--accent); -webkit-text-fill-color: var(--accent); }\n"
"  .host-badge {\n"
"    margin-left: auto;\n"
"    font-size: 0.75rem;\n"
"    color: var(--muted);\n"
"    background: var(--surface);\n"
"    border: 1px solid var(--border);\n"
"    padding: 6px 14px;\n"
"    border-radius: 20px;\n"
"    letter-spacing: 1px;\n"
"  }\n"
"  /* Player section */\n"
"  #player-section {\n"
"    display: none;\n"
"    position: relative;\n"
"  }\n"
"  #player-section.active { display: block; }\n"
"  #player-wrap {\n"
"    background: #000;\n"
"    position: relative;\n"
"  }\n"
"  #player-wrap::after {\n"
"    content: '';\n"
"    position: absolute;\n"
"    inset: 0;\n"
"    background: linear-gradient(to bottom, transparent 60%, rgba(10,10,15,0.9) 100%);\n"
"    pointer-events: none;\n"
"  }\n"
"  video {\n"
"    width: 100%;\n"
"    max-height: 72vh;\n"
"    display: block;\n"
"    object-fit: contain;\n"
"  }\n"
"  #now-playing {\n"
"    position: absolute;\n"
"    bottom: 20px; left: 48px;\n"
"    z-index: 10;\n"
"  }\n"
"  #now-playing .label {\n"
"    font-size: 0.65rem;\n"
"    letter-spacing: 3px;\n"
"    text-transform: uppercase;\n"
"    color: var(--accent);\n"
"    margin-bottom: 4px;\n"
"  }\n"
"  #now-playing .title {\n"
"    font-family: 'Bebas Neue', sans-serif;\n"
"    font-size: 1.8rem;\n"
"    letter-spacing: 1px;\n"
"  }\n"
"  /* Library grid */\n"
"  main { padding: 48px; }\n"
"  .section-label {\n"
"    font-size: 0.65rem;\n"
"    letter-spacing: 4px;\n"
"    text-transform: uppercase;\n"
"    color: var(--muted);\n"
"    margin-bottom: 24px;\n"
"  }\n"
"  #library {\n"
"    display: grid;\n"
"    grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));\n"
"    gap: 16px;\n"
"  }\n"
"  .video-card {\n"
"    background: var(--card);\n"
"    border: 1px solid var(--border);\n"
"    border-radius: 8px;\n"
"    overflow: hidden;\n"
"    cursor: pointer;\n"
"    transition: transform 0.2s ease, border-color 0.2s ease, box-shadow 0.2s ease;\n"
"    position: relative;\n"
"  }\n"
"  .video-card:hover {\n"
"    transform: translateY(-4px);\n"
"    border-color: var(--accent);\n"
"    box-shadow: 0 12px 40px rgba(232, 54, 93, 0.2);\n"
"  }\n"
"  .video-card.active-card {\n"
"    border-color: var(--accent);\n"
"    box-shadow: 0 0 0 2px rgba(232, 54, 93, 0.4);\n"
"  }\n"
"  .card-thumb {\n"
"    height: 130px;\n"
"    background: linear-gradient(135deg, #1a1a28, #0f0f1a);\n"
"    display: flex; align-items: center; justify-content: center;\n"
"    position: relative;\n"
"    overflow: hidden;\n"
"  }\n"
"  .card-thumb::before {\n"
"    content: '';\n"
"    position: absolute; inset: 0;\n"
"    background: linear-gradient(135deg,\n"
"      rgba(232,54,93,0.05) 0%,\n"
"      transparent 50%,\n"
"      rgba(255,107,53,0.05) 100%);\n"
"  }\n"
"  .play-icon {\n"
"    width: 44px; height: 44px;\n"
"    background: rgba(232,54,93,0.15);\n"
"    border: 1.5px solid rgba(232,54,93,0.4);\n"
"    border-radius: 50%;\n"
"    display: flex; align-items: center; justify-content: center;\n"
"    transition: background 0.2s, transform 0.2s;\n"
"  }\n"
"  .video-card:hover .play-icon {\n"
"    background: var(--accent);\n"
"    transform: scale(1.1);\n"
"  }\n"
"  .play-icon svg { margin-left: 3px; }\n"
"  .card-info { padding: 14px 16px; }\n"
"  .card-name {\n"
"    font-size: 0.875rem;\n"
"    font-weight: 500;\n"
"    white-space: nowrap;\n"
"    overflow: hidden;\n"
"    text-overflow: ellipsis;\n"
"    margin-bottom: 4px;\n"
"  }\n"
"  .card-ext {\n"
"    font-size: 0.7rem;\n"
"    color: var(--muted);\n"
"    text-transform: uppercase;\n"
"    letter-spacing: 1px;\n"
"  }\n"
"  .card-size {\n"
"    font-size: 0.7rem;\n"
"    color: var(--muted);\n"
"    float: right;\n"
"  }\n"
"  /* Empty state */\n"
"  .empty {\n"
"    text-align: center;\n"
"    padding: 80px 20px;\n"
"    color: var(--muted);\n"
"    grid-column: 1/-1;\n"
"  }\n"
"  .empty .big { font-size: 3rem; margin-bottom: 16px; }\n"
"  /* Loading */\n"
"  .spinner {\n"
"    width: 32px; height: 32px;\n"
"    border: 2px solid var(--border);\n"
"    border-top-color: var(--accent);\n"
"    border-radius: 50%;\n"
"    animation: spin 0.7s linear infinite;\n"
"    margin: 60px auto;\n"
"  }\n"
"  @keyframes spin { to { transform: rotate(360deg); } }\n"
"  /* Accent line under header */\n"
"  .accent-bar {\n"
"    height: 3px;\n"
"    background: linear-gradient(90deg, var(--accent), var(--accent2), transparent);\n"
"  }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<header>\n"
"  <span class='logo'>Stream<span class='logo-dot'>.</span>Cast</span>\n"
"  <span class='host-badge' id='host-label'>Loading...</span>\n"
"</header>\n"
"<div class='accent-bar'></div>\n"
"\n"
"<div id='player-section'>\n"
"  <div id='player-wrap'>\n"
"    <video id='video' controls autoplay></video>\n"
"    <div id='now-playing'>\n"
"      <div class='label'>Now Playing</div>\n"
"      <div class='title' id='now-title'></div>\n"
"    </div>\n"
"  </div>\n"
"</div>\n"
"\n"
"<main>\n"
"  <div class='section-label'>Video Library</div>\n"
"  <div id='library'><div class='spinner'></div></div>\n"
"</main>\n"
"\n"
"<script>\n"
"const video = document.getElementById('video');\n"
"const playerSection = document.getElementById('player-section');\n"
"const nowTitle = document.getElementById('now-title');\n"
"const library = document.getElementById('library');\n"
"document.getElementById('host-label').textContent = window.location.host;\n"
"\n"
"function formatSize(bytes) {\n"
"  if (bytes >= 1073741824) return (bytes/1073741824).toFixed(1) + ' GB';\n"
"  if (bytes >= 1048576)    return (bytes/1048576).toFixed(1) + ' MB';\n"
"  return (bytes/1024).toFixed(0) + ' KB';\n"
"}\n"
"\n"
"function ext(name) {\n"
"  return (name.split('.').pop() || '').toUpperCase();\n"
"}\n"
"\n"
"function play(name, card) {\n"
"  document.querySelectorAll('.video-card').forEach(c => c.classList.remove('active-card'));\n"
"  card.classList.add('active-card');\n"
"  video.src = '/videos/' + encodeURIComponent(name);\n"
"  nowTitle.textContent = name.replace(/\\.[^.]+$/, '');\n"
"  playerSection.classList.add('active');\n"
"  playerSection.scrollIntoView({ behavior: 'smooth', block: 'start' });\n"
"  video.play();\n"
"}\n"
"\n"
"async function loadLibrary() {\n"
"  try {\n"
"    const res = await fetch('/api/videos');\n"
"    const files = await res.json();\n"
"    if (!files.length) {\n"
"      library.innerHTML = '<div class=\"empty\"><div class=\"big\">📂</div>No videos found in the server folder.</div>';\n"
"      return;\n"
"    }\n"
"    library.innerHTML = '';\n"
"    files.forEach(f => {\n"
"      const card = document.createElement('div');\n"
"      card.className = 'video-card';\n"
"      card.innerHTML = `\n"
"        <div class='card-thumb'>\n"
"          <div class='play-icon'>\n"
"            <svg width='14' height='14' viewBox='0 0 24 24' fill='white'>\n"
"              <path d='M8 5v14l11-7z'/>\n"
"            </svg>\n"
"          </div>\n"
"        </div>\n"
"        <div class='card-info'>\n"
"          <div class='card-name' title='${f.name}'>${f.name.replace(/\\.[^.]+$/, '')}</div>\n"
"          <span class='card-ext'>${ext(f.name)}</span>\n"
"          <span class='card-size'>${formatSize(f.size)}</span>\n"
"        </div>`;\n"
"      card.onclick = () => play(f.name, card);\n"
"      library.appendChild(card);\n"
"    });\n"
"  } catch(e) {\n"
"    library.innerHTML = '<div class=\"empty\"><div class=\"big\">⚠️</div>Failed to load video list.</div>';\n"
"  }\n"
"}\n"
"\n"
"loadLibrary();\n"
"</script>\n"
"</body>\n"
"</html>\n";

/* ─── Parse HTTP request ───────────────────────────────────── */
static int parse_request(Request *req) {
    char buf[8192];
    int total = 0;
    /* Read until double CRLF */
    while (total < (int)sizeof(buf) - 1) {
        int n = recv(req->fd, buf + total, sizeof(buf) - total - 1, 0);
        if (n <= 0) return -1;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }

    /* Parse request line */
    char raw_path[MAX_PATH];
    if (sscanf(buf, "%7s %511s", req->method, raw_path) != 2) return -1;

    /* Strip query string */
    char *q = strchr(raw_path, '?');
    if (q) *q = '\0';

    url_decode(req->path, raw_path, MAX_PATH);

    /* Parse headers */
    req->header_count = 0;
    req->has_range = 0;
    req->range_start = 0;
    req->range_end = -1;

    char *line = strstr(buf, "\r\n");
    while (line && req->header_count < MAX_HEADERS) {
        line += 2;
        if (strncmp(line, "\r\n", 2) == 0) break;
        char *colon = strchr(line, ':');
        if (!colon) break;
        int klen = colon - line;
        if (klen > 255) klen = 255;
        strncpy(req->headers[req->header_count][0], line, klen);
        req->headers[req->header_count][0][klen] = '\0';
        char *val = colon + 1;
        while (*val == ' ') val++;
        char *end = strstr(val, "\r\n");
        int vlen = end ? (int)(end - val) : (int)strlen(val);
        if (vlen > 255) vlen = 255;
        strncpy(req->headers[req->header_count][1], val, vlen);
        req->headers[req->header_count][1][vlen] = '\0';

        /* Check for Range header */
        if (strcasecmp(req->headers[req->header_count][0], "Range") == 0) {
            const char *rv = req->headers[req->header_count][1];
            if (strncmp(rv, "bytes=", 6) == 0) {
                rv += 6;
                long start = 0, end2 = -1;
                if (sscanf(rv, "%ld-%ld", &start, &end2) >= 1) {
                    req->range_start = start;
                    req->range_end   = end2;
                    req->has_range   = 1;
                }
            }
        }
        req->header_count++;
        line = end ? end : line + strlen(line);
    }
    return 0;
}

/* ─── Serve the embedded HTML UI ─────────────────────────── */
static void serve_ui(int fd) {
    int len = (int)strlen(HTML_UI);
    send_fmt(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", len);
    send_all(fd, HTML_UI, len);
}

/* ─── Serve /api/videos JSON ──────────────────────────────── */
static void serve_api_videos(int fd) {
    DIR *d = opendir(videos_dir);
    if (!d) {
        const char *r = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 2\r\n\r\n[]";
        send_all(fd, r, strlen(r));
        return;
    }

    char body[65536];
    int pos = 0;
    body[pos++] = '[';

    struct dirent *ent;
    int first = 1;
    while ((ent = readdir(d)) != NULL) {
        if (!is_video(ent->d_name)) continue;

        /* Get file size */
        char fpath[MAX_PATH];
        snprintf(fpath, sizeof(fpath), "%s/%s", videos_dir, ent->d_name);
        struct stat st;
        long size = 0;
        if (stat(fpath, &st) == 0) size = (long)st.st_size;

        /* Escape name for JSON */
        char escaped[512];
        int ei = 0;
        for (int i = 0; ent->d_name[i] && ei < 500; i++) {
            char c = ent->d_name[i];
            if (c == '"' || c == '\\') escaped[ei++] = '\\';
            escaped[ei++] = c;
        }
        escaped[ei] = '\0';

        int n = snprintf(body + pos, sizeof(body) - pos - 4,
            "%s{\"name\":\"%s\",\"size\":%ld}",
            first ? "" : ",", escaped, size);
        if (n < 0 || pos + n >= (int)sizeof(body) - 4) break;
        pos += n;
        first = 0;
    }
    closedir(d);
    body[pos++] = ']';
    body[pos]   = '\0';

    send_fmt(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n", pos);
    send_all(fd, body, pos);
}

/* ─── Serve video with Range support ─────────────────────── */
static void serve_video(int fd, Request *req, const char *filename) {
    /* Security: reject path traversal */
    if (strstr(filename, "..") || strchr(filename, '/')) {
        const char *r = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        send_all(fd, r, strlen(r));
        return;
    }

    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", videos_dir, filename);

    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        const char *r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send_all(fd, r, strlen(r));
        LOG_WARN("404: %s", filepath);
        return;
    }

    struct stat st;
    fstat(file_fd, &st);
    long file_size = (long)st.st_size;

    long range_start = req->has_range ? req->range_start : 0;
    long range_end   = (req->has_range && req->range_end >= 0)
                         ? req->range_end
                         : file_size - 1;

    /* Clamp */
    if (range_end >= file_size) range_end = file_size - 1;
    if (range_start > range_end || range_start >= file_size) {
        send_fmt(fd,
            "HTTP/1.1 416 Range Not Satisfiable\r\n"
            "Content-Range: bytes */%ld\r\n"
            "Content-Length: 0\r\n\r\n", file_size);
        close(file_fd);
        return;
    }

    long content_len = range_end - range_start + 1;
    const char *mime  = mime_type(filename);
    int partial = req->has_range;

    LOG_INFO("%s %s bytes=%ld-%ld/%ld",
        partial ? "206" : "200", filename,
        range_start, range_end, file_size);

    send_fmt(fd,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Content-Range: bytes %ld-%ld/%ld\r\n"
        "Accept-Ranges: bytes\r\n"
        "Connection: close\r\n"
        "\r\n",
        partial ? "206 Partial Content" : "200 OK",
        mime, content_len,
        range_start, range_end, file_size);

    /* Seek and stream */
    lseek(file_fd, range_start, SEEK_SET);

    char *buf = malloc(SEND_CHUNK);
    if (!buf) { close(file_fd); return; }

    long remaining = content_len;
    while (remaining > 0) {
        long to_read = remaining < SEND_CHUNK ? remaining : SEND_CHUNK;
        ssize_t n = read(file_fd, buf, to_read);
        if (n <= 0) break;
        if (send_all(fd, buf, n) < 0) break;
        remaining -= n;
    }

    free(buf);
    close(file_fd);
}

/* ─── 404 Response ─────────────────────────────────────────── */
static void serve_404(int fd) {
    const char *body = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot Found";
    send_all(fd, body, strlen(body));
}

/* ─── Handle one client ─────────────────────────────────────── */
static void *handle_client(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    Request req;
    memset(&req, 0, sizeof(req));
    req.fd = fd;

    if (parse_request(&req) < 0) goto done;

    LOG_INFO("%s %s", req.method, req.path);

    /* Route */
    if (strcmp(req.path, "/") == 0 || strcmp(req.path, "/index.html") == 0) {
        serve_ui(fd);
    } else if (strcmp(req.path, "/api/videos") == 0) {
        serve_api_videos(fd);
    } else if (strncmp(req.path, "/videos/", 8) == 0) {
        serve_video(fd, &req, req.path + 8);
    } else {
        serve_404(fd);
    }

done:
    close(fd);
    return NULL;
}

/* ─── Main ─────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc >= 2) strncpy(videos_dir, argv[1], MAX_PATH - 1);
    else           strncpy(videos_dir, "./videos", MAX_PATH - 1);

    if (argc >= 3) port = atoi(argv[2]);

    /* Verify videos dir exists */
    struct stat st;
    if (stat(videos_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a directory\n", videos_dir);
        return 1;
    }

    /* Ignore broken pipes */
    signal(SIGPIPE, SIG_IGN);

    /* Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(port)
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen"); return 1;
    }

    printf("\n");
    printf("  ╔══════════════════════════════════════╗\n");
    printf("  ║      StreamCast — Video Server       ║\n");
    printf("  ╚══════════════════════════════════════╝\n\n");
    printf("  Videos dir : %s\n", videos_dir);
    printf("  Port       : %d\n\n", port);
    printf("  Open in browser:\n");
    printf("    → http://localhost:%d\n", port);
    printf("    → http://<your-hotspot-ip>:%d  (for other devices)\n\n", port);
    printf("  Press Ctrl+C to stop.\n\n");

    /* Accept loop */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        /* Spawn thread per client */
        int *pfd = malloc(sizeof(int));
        *pfd = client_fd;
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, pfd) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(pfd);
        } else {
            pthread_detach(tid);
        }
    }

    close(server_fd);
    return 0;
}
