#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT 8085
#define BACKLOG 64
#define MAX_HEADER_SIZE 65536
#define MAX_BODY_SIZE (8 * 1024 * 1024)
#define IO_CHUNK 65536
#define VIDEO_CACHE_TTL 10
#define SESSION_TTL_SECONDS 3600
#define MAX_SESSIONS 512
#define MAX_LIVE_VIEWERS 256
#define MAX_CHAT_SUBSCRIBERS 256
#define CHAT_BACKLOG 32
#define MAX_MSG_LEN 256

static char g_video_dir[512] = "../db/videos";
static char g_users_db[512] = "../db/users.db";
static char g_static_dir[512] = "./static";

struct Header {
    char key[64];
    char value[1024];
};

struct HttpRequest {
    char method[16];
    char target[1024];
    char path[1024];
    char query[1024];
    char version[16];
    struct Header headers[64];
    int header_count;
    unsigned char *body;
    size_t body_len;
};

struct VideoMeta {
    char name[256];
    off_t size;
};

struct VideoCache {
    struct VideoMeta items[1024];
    int count;
    time_t expires_at;
};

struct Session {
    int used;
    char token[65];
    char username[64];
    time_t expires_at;
};

struct LiveViewer {
    int active;
    int fd;
};

struct ChatSubscriber {
    int active;
    int fd;
};

struct ChatMessage {
    char username[64];
    char message[MAX_MSG_LEN];
    char timestamp[32];
};

static pthread_mutex_t g_video_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_sessions_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_live_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_chat_lock = PTHREAD_MUTEX_INITIALIZER;

static struct VideoCache g_video_cache = {0};
static struct Session g_sessions[MAX_SESSIONS] = {0};
static struct LiveViewer g_live_viewers[MAX_LIVE_VIEWERS] = {0};
static struct ChatSubscriber g_chat_subscribers[MAX_CHAT_SUBSCRIBERS] = {0};
static struct ChatMessage g_chat_messages[CHAT_BACKLOG] = {0};
static int g_chat_count = 0;
static int g_chat_start = 0;

/* ===================== SHA-256 ===================== */

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;

#define ROTLEFT(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x, 2) ^ ROTRIGHT(x, 13) ^ ROTRIGHT(x, 22))
#define EP1(x) (ROTRIGHT(x, 6) ^ ROTRIGHT(x, 11) ^ ROTRIGHT(x, 25))
#define SIG0(x) (ROTRIGHT(x, 7) ^ ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x, 17) ^ ROTRIGHT(x, 19) ^ ((x) >> 10))

static const uint32_t k256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];
    for (i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    }
    for (; i < 64; ++i) m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + k256[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }
    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);
    for (i = 0; i < 4; ++i) {
        hash[i] = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 4] = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 8] = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0x000000ff);
    }
}

/* ===================== Utility ===================== */

static void now_http_date(char *out, size_t out_len, time_t t) {
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(out, out_len, "%a, %d %b %Y %H:%M:%S GMT", &tmv);
}

static void lowercase(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static int send_all(int fd, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static const char *mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcasecmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(dot, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcasecmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(dot, ".mp4") == 0) return "video/mp4";
    if (strcasecmp(dot, ".webm") == 0) return "video/webm";
    if (strcasecmp(dot, ".ogg") == 0 || strcasecmp(dot, ".ogv") == 0) return "video/ogg";
    if (strcasecmp(dot, ".mkv") == 0) return "video/x-matroska";
    if (strcasecmp(dot, ".mov") == 0) return "video/quicktime";
    if (strcasecmp(dot, ".avi") == 0) return "video/x-msvideo";
    return "application/octet-stream";
}

static int is_safe_name(const char *s) {
    if (!s || !*s) return 0;
    if (strstr(s, "..") || strchr(s, '/') || strchr(s, '\\')) return 0;
    return 1;
}

static int is_safe_username(const char *s) {
    if (!s) return 0;
    size_t n = strlen(s);
    if (n < 3 || n > 48) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}

static int fill_random(unsigned char *buf, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, len);
        close(fd);
        return n == (ssize_t)len ? 0 : -1;
    }
    return -1;
}

static void hex_encode(const unsigned char *src, size_t len, char *dst, size_t dst_len) {
    static const char *hex = "0123456789abcdef";
    if (dst_len < len * 2 + 1) return;
    for (size_t i = 0; i < len; i++) {
        dst[i * 2] = hex[src[i] >> 4];
        dst[i * 2 + 1] = hex[src[i] & 0x0F];
    }
    dst[len * 2] = '\0';
}

static void url_decode(char *s) {
    char *src = s;
    char *dst = s;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static const char *req_header(const struct HttpRequest *req, const char *key_lc) {
    for (int i = 0; i < req->header_count; i++) {
        if (strcmp(req->headers[i].key, key_lc) == 0) return req->headers[i].value;
    }
    return NULL;
}

static int form_value(const unsigned char *body, size_t body_len, const char *key, char *out, size_t out_len) {
    char *tmp = calloc(body_len + 1, 1);
    if (!tmp) return -1;
    memcpy(tmp, body, body_len);
    int found = 0;
    char *saveptr = NULL;
    for (char *part = strtok_r(tmp, "&", &saveptr); part; part = strtok_r(NULL, "&", &saveptr)) {
        char *eq = strchr(part, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = part;
        char *v = eq + 1;
        url_decode(k);
        url_decode(v);
        if (strcmp(k, key) == 0) {
            if (strlen(v) >= out_len) {
                free(tmp);
                return -2;
            }
            snprintf(out, out_len, "%s", v);
            found = 1;
            break;
        }
    }
    free(tmp);
    return found ? 0 : -1;
}

static void json_escape(const char *src, char *dst, size_t dst_len) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 2 < dst_len; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (c == '\n' || c == '\r') {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if ((unsigned char)c >= 0x20) {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

/* ===================== Request parsing ===================== */

static int parse_request(int client, struct HttpRequest *req) {
    memset(req, 0, sizeof(*req));
    unsigned char *buf = malloc(MAX_HEADER_SIZE + MAX_BODY_SIZE + 1);
    if (!buf) return -1;
    size_t total = 0;
    size_t header_end = 0;
    while (total < MAX_HEADER_SIZE) {
        ssize_t n = recv(client, buf + total, MAX_HEADER_SIZE - total, 0);
        if (n <= 0) {
            free(buf);
            return -1;
        }
        total += (size_t)n;
        for (size_t i = 3; i < total; i++) {
            if (buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n') {
                header_end = i + 1;
                break;
            }
        }
        if (header_end) break;
    }
    if (!header_end) {
        free(buf);
        return -1;
    }
    char *header_text = calloc(header_end + 1, 1);
    if (!header_text) {
        free(buf);
        return -1;
    }
    memcpy(header_text, buf, header_end);
    char *save_line = NULL;
    char *line = strtok_r(header_text, "\r\n", &save_line);
    if (!line) {
        free(header_text);
        free(buf);
        return -1;
    }
    if (sscanf(line, "%15s %1023s %15s", req->method, req->target, req->version) != 3) {
        free(header_text);
        free(buf);
        return -1;
    }
    char *q = strchr(req->target, '?');
    if (q) {
        *q = '\0';
        snprintf(req->path, sizeof(req->path), "%s", req->target);
        snprintf(req->query, sizeof(req->query), "%s", q + 1);
    } else {
        snprintf(req->path, sizeof(req->path), "%s", req->target);
        req->query[0] = '\0';
    }
    while ((line = strtok_r(NULL, "\r\n", &save_line)) != NULL) {
        if (!*line) continue;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *k = line;
        char *v = colon + 1;
        while (*v == ' ') v++;
        if (req->header_count < (int)(sizeof(req->headers) / sizeof(req->headers[0]))) {
            snprintf(req->headers[req->header_count].key, sizeof(req->headers[req->header_count].key), "%s", k);
            lowercase(req->headers[req->header_count].key);
            snprintf(req->headers[req->header_count].value, sizeof(req->headers[req->header_count].value), "%s", v);
            req->header_count++;
        }
    }
    size_t content_len = 0;
    const char *cl = req_header(req, "content-length");
    if (cl) content_len = (size_t)strtoull(cl, NULL, 10);
    if (content_len > MAX_BODY_SIZE) {
        free(header_text);
        free(buf);
        return -2;
    }
    req->body_len = content_len;
    if (content_len > 0) {
        req->body = malloc(content_len);
        if (!req->body) {
            free(header_text);
            free(buf);
            return -1;
        }
        size_t have = total - header_end;
        if (have > content_len) have = content_len;
        if (have > 0) memcpy(req->body, buf + header_end, have);
        while (have < content_len) {
            ssize_t n = recv(client, req->body + have, content_len - have, 0);
            if (n <= 0) {
                free(req->body);
                req->body = NULL;
                free(header_text);
                free(buf);
                return -1;
            }
            have += (size_t)n;
        }
    }
    free(header_text);
    free(buf);
    return 0;
}

static void free_request(struct HttpRequest *req) {
    if (req->body) free(req->body);
}

/* ===================== HTTP responses ===================== */

static void send_simple_response(int client, int status, const char *status_text, const char *content_type, const char *body) {
    size_t body_len = body ? strlen(body) : 0;
    char header[1024];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "Cache-Control: no-store\r\n\r\n",
             status, status_text, content_type, body_len);
    send_all(client, header, strlen(header));
    if (body_len) send_all(client, body, body_len);
}

static void send_json_ok(int client, const char *json) {
    send_simple_response(client, 200, "OK", "application/json; charset=utf-8", json);
}

static void send_json_status(int client, int code, const char *status_text, const char *json) {
    send_simple_response(client, code, status_text, "application/json; charset=utf-8", json);
}

static void send_redirect(int client, const char *location) {
    char resp[1024];
    snprintf(resp, sizeof(resp),
             "HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
             location);
    send_all(client, resp, strlen(resp));
}

/* ===================== Auth ===================== */

static void ensure_users_db(void) {
    int fd = open(g_users_db, O_CREAT | O_RDWR, 0644);
    if (fd >= 0) close(fd);
}

static void password_hash_hex(const char *salt_hex, const char *password, char *out_hex, size_t out_len) {
    unsigned char digest[32];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)salt_hex, strlen(salt_hex));
    sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    sha256_final(&ctx, digest);
    hex_encode(digest, sizeof(digest), out_hex, out_len);
}

static int user_exists(const char *username) {
    FILE *fp = fopen(g_users_db, "r");
    if (!fp) return 0;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char u[64], s[128], h[128];
        if (sscanf(line, "%63[^:]:%127[^:]:%127s", u, s, h) == 3 && strcmp(u, username) == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

static int register_user(const char *username, const char *password) {
    if (!is_safe_username(username)) return -1;
    if (!password || strlen(password) < 8 || strlen(password) > 128) return -1;
    ensure_users_db();
    if (user_exists(username)) return 1;
    unsigned char salt[16];
    char salt_hex[33];
    char hash_hex[65];
    if (fill_random(salt, sizeof(salt)) != 0) return -1;
    hex_encode(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    password_hash_hex(salt_hex, password, hash_hex, sizeof(hash_hex));
    FILE *fp = fopen(g_users_db, "a");
    if (!fp) return -1;
    fprintf(fp, "%s:%s:%s\n", username, salt_hex, hash_hex);
    fclose(fp);
    return 0;
}

static int verify_user_password(const char *username, const char *password) {
    FILE *fp = fopen(g_users_db, "r");
    if (!fp) return 0;
    char line[512];
    int ok = 0;
    while (fgets(line, sizeof(line), fp)) {
        char u[64], salt[128], stored_hash[128];
        if (sscanf(line, "%63[^:]:%127[^:]:%127s", u, salt, stored_hash) == 3 && strcmp(u, username) == 0) {
            char actual_hash[65];
            password_hash_hex(salt, password, actual_hash, sizeof(actual_hash));
            if (strcmp(actual_hash, stored_hash) == 0) ok = 1;
            break;
        }
    }
    fclose(fp);
    return ok;
}

static void cleanup_sessions_locked(void) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].used && g_sessions[i].expires_at < now) g_sessions[i].used = 0;
    }
}

static int create_session(const char *username, char *out_token, size_t out_token_len) {
    unsigned char rnd[32];
    if (fill_random(rnd, sizeof(rnd)) != 0) return -1;
    char token[65];
    hex_encode(rnd, sizeof(rnd), token, sizeof(token));
    pthread_mutex_lock(&g_sessions_lock);
    cleanup_sessions_locked();
    int idx = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!g_sessions[i].used) {
            idx = i;
            break;
        }
    }
    if (idx >= 0) {
        g_sessions[idx].used = 1;
        snprintf(g_sessions[idx].token, sizeof(g_sessions[idx].token), "%s", token);
        snprintf(g_sessions[idx].username, sizeof(g_sessions[idx].username), "%s", username);
        g_sessions[idx].expires_at = time(NULL) + SESSION_TTL_SECONDS;
    }
    pthread_mutex_unlock(&g_sessions_lock);
    if (idx < 0) return -1;
    snprintf(out_token, out_token_len, "%s", token);
    return 0;
}

static int parse_cookie_value(const char *cookie_header, const char *name, char *out, size_t out_len) {
    if (!cookie_header) return -1;
    char *tmp = strdup(cookie_header);
    if (!tmp) return -1;
    int found = -1;
    char *save = NULL;
    for (char *part = strtok_r(tmp, ";", &save); part; part = strtok_r(NULL, ";", &save)) {
        while (*part == ' ') part++;
        char *eq = strchr(part, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(part, name) == 0) {
            snprintf(out, out_len, "%s", eq + 1);
            found = 0;
            break;
        }
    }
    free(tmp);
    return found;
}

static int session_user_from_request(const struct HttpRequest *req, char *username, size_t username_len, char *token_out, size_t token_len) {
    const char *cookie = req_header(req, "cookie");
    char token[128];
    if (parse_cookie_value(cookie, "session_token", token, sizeof(token)) != 0) return 0;
    int ok = 0;
    pthread_mutex_lock(&g_sessions_lock);
    cleanup_sessions_locked();
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].used && strcmp(g_sessions[i].token, token) == 0) {
            snprintf(username, username_len, "%s", g_sessions[i].username);
            if (token_out) snprintf(token_out, token_len, "%s", token);
            g_sessions[i].expires_at = time(NULL) + SESSION_TTL_SECONDS;
            ok = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_sessions_lock);
    return ok;
}

static void invalidate_session_by_token(const char *token) {
    pthread_mutex_lock(&g_sessions_lock);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].used && strcmp(g_sessions[i].token, token) == 0) {
            g_sessions[i].used = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_sessions_lock);
}

/* ===================== Video cache ===================== */

static int is_video_name(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return strcasecmp(dot, ".mp4") == 0 || strcasecmp(dot, ".webm") == 0 ||
           strcasecmp(dot, ".mkv") == 0 || strcasecmp(dot, ".ogg") == 0 ||
           strcasecmp(dot, ".ogv") == 0 || strcasecmp(dot, ".avi") == 0 ||
           strcasecmp(dot, ".mov") == 0;
}

static void refresh_video_cache_locked(void) {
    DIR *dir = opendir(g_video_dir);
    g_video_cache.count = 0;
    if (!dir) {
        g_video_cache.expires_at = time(NULL) + VIDEO_CACHE_TTL;
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.' || !is_video_name(ent->d_name)) continue;
        if (g_video_cache.count >= (int)(sizeof(g_video_cache.items) / sizeof(g_video_cache.items[0]))) break;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", g_video_dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        struct VideoMeta *m = &g_video_cache.items[g_video_cache.count++];
        snprintf(m->name, sizeof(m->name), "%s", ent->d_name);
        m->size = st.st_size;
    }
    closedir(dir);
    g_video_cache.expires_at = time(NULL) + VIDEO_CACHE_TTL;
}

static char *video_list_json(void) {
    pthread_mutex_lock(&g_video_cache_lock);
    if (time(NULL) >= g_video_cache.expires_at) refresh_video_cache_locked();
    size_t cap = 128 + (size_t)g_video_cache.count * 384;
    char *json = malloc(cap);
    if (!json) {
        pthread_mutex_unlock(&g_video_cache_lock);
        return NULL;
    }
    size_t n = 0;
    int wrote = snprintf(json + n, cap - n, "{\"videos\":[");
    if (wrote < 0 || (size_t)wrote >= cap - n) {
        pthread_mutex_unlock(&g_video_cache_lock);
        free(json);
        return NULL;
    }
    n += (size_t)wrote;
    for (int i = 0; i < g_video_cache.count; i++) {
        char esc[512];
        json_escape(g_video_cache.items[i].name, esc, sizeof(esc));
        wrote = snprintf(json + n, cap - n, "%s{\"name\":\"%s\",\"size\":%lld,\"duration\":null}",
                         i ? "," : "", esc, (long long)g_video_cache.items[i].size);
        if (wrote < 0 || (size_t)wrote >= cap - n) {
            pthread_mutex_unlock(&g_video_cache_lock);
            free(json);
            return NULL;
        }
        n += (size_t)wrote;
    }
    wrote = snprintf(json + n, cap - n, "]}");
    if (wrote < 0 || (size_t)wrote >= cap - n) {
        pthread_mutex_unlock(&g_video_cache_lock);
        free(json);
        return NULL;
    }
    pthread_mutex_unlock(&g_video_cache_lock);
    return json;
}

/* ===================== Static with cache headers ===================== */

static void build_etag(const struct stat *st, char *etag, size_t etag_len) {
    snprintf(etag, etag_len, "\"%llx-%llx\"", (long long)st->st_mtime, (long long)st->st_size);
}

static int parse_http_date(const char *s, time_t *out) {
    if (!s) return -1;
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    char *r = strptime(s, "%a, %d %b %Y %H:%M:%S GMT", &tmv);
    if (!r) return -1;
    *out = timegm(&tmv);
    return 0;
}

static void serve_static_file(int client, const struct HttpRequest *req, const char *full_path) {
    int fd = open(full_path, O_RDONLY);
    if (fd < 0) {
        send_simple_response(client, 404, "Not Found", "text/plain; charset=utf-8", "Not Found");
        return;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        send_simple_response(client, 404, "Not Found", "text/plain; charset=utf-8", "Not Found");
        return;
    }
    char etag[64];
    build_etag(&st, etag, sizeof(etag));
    const char *inm = req_header(req, "if-none-match");
    if (inm && strcmp(inm, etag) == 0) {
        char header[512];
        snprintf(header, sizeof(header),
                 "HTTP/1.1 304 Not Modified\r\nETag: %s\r\nCache-Control: public, max-age=30\r\nConnection: close\r\n\r\n",
                 etag);
        send_all(client, header, strlen(header));
        return;
    }
    const char *ims = req_header(req, "if-modified-since");
    if (ims) {
        time_t ims_t;
        if (parse_http_date(ims, &ims_t) == 0 && st.st_mtime <= ims_t) {
            char header[512];
            snprintf(header, sizeof(header),
                     "HTTP/1.1 304 Not Modified\r\nETag: %s\r\nCache-Control: public, max-age=30\r\nConnection: close\r\n\r\n",
                     etag);
            send_all(client, header, strlen(header));
            return;
        }
    }
    char lm[64];
    now_http_date(lm, sizeof(lm), st.st_mtime);
    char header[1024];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\nLast-Modified: %s\r\nETag: %s\r\nCache-Control: public, max-age=30\r\nConnection: close\r\n\r\n",
             mime_type(full_path), (long long)st.st_size, lm, etag);
    send_all(client, header, strlen(header));
    char buf[IO_CHUNK];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (send_all(client, buf, (size_t)n) != 0) break;
    }
    close(fd);
}

/* ===================== Video serving with Range ===================== */

static void serve_video_file(int client, const struct HttpRequest *req, const char *filename) {
    if (!is_safe_name(filename)) {
        send_simple_response(client, 400, "Bad Request", "text/plain; charset=utf-8", "Invalid filename");
        return;
    }
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", g_video_dir, filename);
    int fd = open(full, O_RDONLY);
    if (fd < 0) {
        send_simple_response(client, 404, "Not Found", "text/plain; charset=utf-8", "Video not found");
        return;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        send_simple_response(client, 404, "Not Found", "text/plain; charset=utf-8", "Video not found");
        return;
    }
    off_t start = 0;
    off_t end = st.st_size - 1;
    int partial = 0;
    const char *range = req_header(req, "range");
    if (range && strncmp(range, "bytes=", 6) == 0) {
        const char *spec = range + 6;
        char *dash = strchr(spec, '-');
        if (dash) {
            char left[64] = {0};
            char right[64] = {0};
            size_t l = (size_t)(dash - spec);
            if (l >= sizeof(left)) l = sizeof(left) - 1;
            memcpy(left, spec, l);
            snprintf(right, sizeof(right), "%s", dash + 1);
            if (left[0]) start = (off_t)strtoll(left, NULL, 10);
            if (right[0]) end = (off_t)strtoll(right, NULL, 10);
            if (start < 0) start = 0;
            if (end >= st.st_size) end = st.st_size - 1;
            if (start <= end && start < st.st_size) partial = 1;
            else {
                close(fd);
                send_simple_response(client, 416, "Range Not Satisfiable", "text/plain; charset=utf-8", "Invalid range");
                return;
            }
        }
    }
    off_t len = end - start + 1;
    char header[1024];
    if (partial) {
        snprintf(header, sizeof(header),
                 "HTTP/1.1 206 Partial Content\r\nContent-Type: %s\r\nAccept-Ranges: bytes\r\nContent-Range: bytes %lld-%lld/%lld\r\nContent-Length: %lld\r\nConnection: close\r\n\r\n",
                 mime_type(full), (long long)start, (long long)end, (long long)st.st_size, (long long)len);
    } else {
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nAccept-Ranges: bytes\r\nContent-Length: %lld\r\nConnection: close\r\n\r\n",
                 mime_type(full), (long long)st.st_size);
    }
    send_all(client, header, strlen(header));
    if (lseek(fd, start, SEEK_SET) >= 0) {
        char buf[IO_CHUNK];
        off_t remaining = len;
        while (remaining > 0) {
            size_t want = remaining > (off_t)sizeof(buf) ? sizeof(buf) : (size_t)remaining;
            ssize_t n = read(fd, buf, want);
            if (n <= 0) break;
            if (send_all(client, buf, (size_t)n) != 0) break;
            remaining -= n;
        }
    }
    close(fd);
}

/* ===================== Live chunked video ===================== */

static void live_viewers_add(int fd) {
    pthread_mutex_lock(&g_live_lock);
    for (int i = 0; i < MAX_LIVE_VIEWERS; i++) {
        if (!g_live_viewers[i].active) {
            g_live_viewers[i].active = 1;
            g_live_viewers[i].fd = fd;
            break;
        }
    }
    pthread_mutex_unlock(&g_live_lock);
}

static void live_viewers_remove(int fd) {
    pthread_mutex_lock(&g_live_lock);
    for (int i = 0; i < MAX_LIVE_VIEWERS; i++) {
        if (g_live_viewers[i].active && g_live_viewers[i].fd == fd) {
            g_live_viewers[i].active = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_live_lock);
}

static void broadcast_live_chunk(const unsigned char *data, size_t len) {
    if (!data || !len) return;
    pthread_mutex_lock(&g_live_lock);
    char chunk_head[64];
    int head_n = snprintf(chunk_head, sizeof(chunk_head), "%zx\r\n", len);
    if (head_n <= 0 || (size_t)head_n >= sizeof(chunk_head)) {
        pthread_mutex_unlock(&g_live_lock);
        return;
    }
    for (int i = 0; i < MAX_LIVE_VIEWERS; i++) {
        if (!g_live_viewers[i].active) continue;
        int fd = g_live_viewers[i].fd;
        if (send_all(fd, chunk_head, (size_t)head_n) != 0 ||
            send_all(fd, data, len) != 0 ||
            send_all(fd, "\r\n", 2) != 0) {
            close(fd);
            g_live_viewers[i].active = 0;
        }
    }
    pthread_mutex_unlock(&g_live_lock);
}

/* ===================== Chat SSE ===================== */

static void chat_add_subscriber(int fd) {
    pthread_mutex_lock(&g_chat_lock);
    for (int i = 0; i < MAX_CHAT_SUBSCRIBERS; i++) {
        if (!g_chat_subscribers[i].active) {
            g_chat_subscribers[i].active = 1;
            g_chat_subscribers[i].fd = fd;
            break;
        }
    }
    pthread_mutex_unlock(&g_chat_lock);
}

static void chat_remove_subscriber(int fd) {
    pthread_mutex_lock(&g_chat_lock);
    for (int i = 0; i < MAX_CHAT_SUBSCRIBERS; i++) {
        if (g_chat_subscribers[i].active && g_chat_subscribers[i].fd == fd) {
            g_chat_subscribers[i].active = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_chat_lock);
}

static void chat_push_backlog(const char *username, const char *message) {
    struct ChatMessage msg;
    snprintf(msg.username, sizeof(msg.username), "%s", username);
    snprintf(msg.message, sizeof(msg.message), "%s", message);
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    strftime(msg.timestamp, sizeof(msg.timestamp), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    if (g_chat_count < CHAT_BACKLOG) {
        int idx = (g_chat_start + g_chat_count) % CHAT_BACKLOG;
        g_chat_messages[idx] = msg;
        g_chat_count++;
    } else {
        g_chat_messages[g_chat_start] = msg;
        g_chat_start = (g_chat_start + 1) % CHAT_BACKLOG;
    }
}

static void chat_broadcast_json_locked(const char *json_data) {
    char packet[1024];
    int n = snprintf(packet, sizeof(packet), "event: message\ndata: %s\n\n", json_data);
    if (n < 0) return;
    for (int i = 0; i < MAX_CHAT_SUBSCRIBERS; i++) {
        if (!g_chat_subscribers[i].active) continue;
        int fd = g_chat_subscribers[i].fd;
        if (send_all(fd, packet, (size_t)n) != 0) {
            close(fd);
            g_chat_subscribers[i].active = 0;
        }
    }
}

static void chat_broadcast_message(const char *username, const char *message) {
    char user_esc[256], msg_esc[512];
    json_escape(username, user_esc, sizeof(user_esc));
    json_escape(message, msg_esc, sizeof(msg_esc));
    pthread_mutex_lock(&g_chat_lock);
    chat_push_backlog(username, message);
    int latest_idx = (g_chat_start + g_chat_count - 1 + CHAT_BACKLOG) % CHAT_BACKLOG;
    char json[900];
    snprintf(json, sizeof(json), "{\"user\":\"%s\",\"message\":\"%s\",\"timestamp\":\"%s\"}",
             user_esc, msg_esc, g_chat_messages[latest_idx].timestamp);
    chat_broadcast_json_locked(json);
    pthread_mutex_unlock(&g_chat_lock);
}

/* ===================== Route handlers ===================== */

static int require_auth_json(int client, const struct HttpRequest *req, char *username_out, size_t username_len) {
    if (!session_user_from_request(req, username_out, username_len, NULL, 0)) {
        send_json_status(client, 401, "Unauthorized", "{\"error\":\"authentication required\"}");
        return 0;
    }
    return 1;
}

static void handle_register(int client, const struct HttpRequest *req) {
    if (strcasecmp(req->method, "POST") != 0) {
        send_json_status(client, 405, "Method Not Allowed", "{\"error\":\"POST required\"}");
        return;
    }
    char username[64] = {0};
    char password[128] = {0};
    int u = form_value(req->body, req->body_len, "username", username, sizeof(username));
    int p = form_value(req->body, req->body_len, "password", password, sizeof(password));
    if (u != 0 || p != 0) {
        send_json_status(client, 400, "Bad Request", "{\"error\":\"username and password are required\"}");
        return;
    }
    int rc = register_user(username, password);
    if (rc == 1) send_json_status(client, 409, "Conflict", "{\"error\":\"user already exists\"}");
    else if (rc != 0) send_json_status(client, 400, "Bad Request", "{\"error\":\"invalid username/password\"}");
    else send_json_ok(client, "{\"ok\":true}");
}

static void handle_login(int client, const struct HttpRequest *req) {
    if (strcasecmp(req->method, "POST") != 0) {
        send_json_status(client, 405, "Method Not Allowed", "{\"error\":\"POST required\"}");
        return;
    }
    char username[64] = {0};
    char password[128] = {0};
    int u = form_value(req->body, req->body_len, "username", username, sizeof(username));
    int p = form_value(req->body, req->body_len, "password", password, sizeof(password));
    if (u != 0 || p != 0) {
        send_json_status(client, 400, "Bad Request", "{\"error\":\"username and password are required\"}");
        return;
    }
    if (!verify_user_password(username, password)) {
        send_json_status(client, 401, "Unauthorized", "{\"error\":\"invalid credentials\"}");
        return;
    }
    char token[65];
    if (create_session(username, token, sizeof(token)) != 0) {
        send_json_status(client, 500, "Internal Server Error", "{\"error\":\"failed to create session\"}");
        return;
    }
    char body[160];
    snprintf(body, sizeof(body), "{\"ok\":true,\"username\":\"%s\"}", username);
    char header[2048];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
             "Set-Cookie: session_token=%s; Path=/; HttpOnly; Max-Age=%d; SameSite=Lax\r\n"
             "Content-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n%s",
             token, SESSION_TTL_SECONDS, strlen(body), body);
    send_all(client, header, strlen(header));
}

static void handle_logout(int client, const struct HttpRequest *req) {
    if (strcasecmp(req->method, "POST") != 0) {
        send_json_status(client, 405, "Method Not Allowed", "{\"error\":\"POST required\"}");
        return;
    }
    char token[128];
    const char *cookie = req_header(req, "cookie");
    if (parse_cookie_value(cookie, "session_token", token, sizeof(token)) == 0) invalidate_session_by_token(token);
    const char *body = "{\"ok\":true}";
    char resp[1024];
    snprintf(resp, sizeof(resp),
             "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
             "Set-Cookie: session_token=; Path=/; HttpOnly; Max-Age=0; SameSite=Lax\r\n"
             "Content-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n%s",
             strlen(body), body);
    send_all(client, resp, strlen(resp));
}

static void handle_whoami(int client, const struct HttpRequest *req) {
    char username[64];
    if (!session_user_from_request(req, username, sizeof(username), NULL, 0)) {
        send_json_ok(client, "{\"authenticated\":false}");
        return;
    }
    char body[160];
    snprintf(body, sizeof(body), "{\"authenticated\":true,\"username\":\"%s\"}", username);
    send_json_ok(client, body);
}

static void handle_chat_send(int client, const struct HttpRequest *req) {
    if (strcasecmp(req->method, "POST") != 0) {
        send_json_status(client, 405, "Method Not Allowed", "{\"error\":\"POST required\"}");
        return;
    }
    char username[64];
    if (!require_auth_json(client, req, username, sizeof(username))) return;
    char message[MAX_MSG_LEN] = {0};
    int m = form_value(req->body, req->body_len, "message", message, sizeof(message));
    if (m == -2) {
        send_json_status(client, 400, "Bad Request", "{\"error\":\"message too long\"}");
        return;
    }
    if (m != 0) {
        send_json_status(client, 400, "Bad Request", "{\"error\":\"message is required\"}");
        return;
    }
    size_t mlen = strlen(message);
    while (mlen > 0 && (message[mlen - 1] == '\r' || message[mlen - 1] == '\n')) message[--mlen] = '\0';
    if (mlen == 0) {
        send_json_status(client, 400, "Bad Request", "{\"error\":\"message is empty\"}");
        return;
    }
    chat_broadcast_message(username, message);
    send_json_ok(client, "{\"ok\":true}");
}

static int handle_chat_stream(int client) {
    const char *header =
        "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n";
    if (send_all(client, header, strlen(header)) != 0) {
        close(client);
        return 1;
    }
    chat_add_subscriber(client);
    pthread_mutex_lock(&g_chat_lock);
    for (int i = 0; i < g_chat_count; i++) {
        int idx = (g_chat_start + i) % CHAT_BACKLOG;
        char user_esc[256], msg_esc[512], json[900];
        json_escape(g_chat_messages[idx].username, user_esc, sizeof(user_esc));
        json_escape(g_chat_messages[idx].message, msg_esc, sizeof(msg_esc));
        snprintf(json, sizeof(json), "{\"user\":\"%s\",\"message\":\"%s\",\"timestamp\":\"%s\"}",
                 user_esc, msg_esc, g_chat_messages[idx].timestamp);
        char packet[1024];
        int n = snprintf(packet, sizeof(packet), "event: message\ndata: %s\n\n", json);
        if (send_all(client, packet, (size_t)n) != 0) {
            pthread_mutex_unlock(&g_chat_lock);
            chat_remove_subscriber(client);
            close(client);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_chat_lock);
    while (1) {
        sleep(15);
        pthread_mutex_lock(&g_chat_lock);
        int rc = send_all(client, ": keep-alive\n\n", 14);
        pthread_mutex_unlock(&g_chat_lock);
        if (rc != 0) break;
        char x;
        ssize_t p = recv(client, &x, 1, MSG_PEEK | MSG_DONTWAIT);
        if (p == 0) break;
        if (p < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;
    }
    chat_remove_subscriber(client);
    close(client);
    return 1;
}

static int handle_live_watch(int client) {
    const char *header =
        "HTTP/1.1 200 OK\r\nContent-Type: video/webm\r\nTransfer-Encoding: chunked\r\n"
        "Cache-Control: no-store\r\nConnection: keep-alive\r\n\r\n";
    if (send_all(client, header, strlen(header)) != 0) {
        close(client);
        return 1;
    }
    live_viewers_add(client);
    while (1) {
        sleep(1);
        char x;
        ssize_t p = recv(client, &x, 1, MSG_PEEK | MSG_DONTWAIT);
        if (p == 0) break;
        if (p < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;
    }
    live_viewers_remove(client);
    close(client);
    return 1;
}

static void handle_live_broadcast(int client, const struct HttpRequest *req) {
    if (strcasecmp(req->method, "POST") != 0) {
        send_json_status(client, 405, "Method Not Allowed", "{\"error\":\"POST required\"}");
        return;
    }
    char username[64];
    if (!require_auth_json(client, req, username, sizeof(username))) return;
    if (req->body_len == 0) {
        send_json_status(client, 400, "Bad Request", "{\"error\":\"empty chunk\"}");
        return;
    }
    broadcast_live_chunk(req->body, req->body_len);
    send_json_ok(client, "{\"ok\":true}");
}

static void handle_videos_api(int client) {
    char *json = video_list_json();
    if (!json) {
        send_json_status(client, 500, "Internal Server Error", "{\"error\":\"failed to build response\"}");
        return;
    }
    send_json_ok(client, json);
    free(json);
}

static int process_request(int client, const struct HttpRequest *req) {
    if (strcmp(req->path, "/api/videos") == 0) {
        handle_videos_api(client);
        return 0;
    }
    if (strcmp(req->path, "/api/auth/register") == 0) {
        handle_register(client, req);
        return 0;
    }
    if (strcmp(req->path, "/api/auth/login") == 0) {
        handle_login(client, req);
        return 0;
    }
    if (strcmp(req->path, "/api/auth/logout") == 0) {
        handle_logout(client, req);
        return 0;
    }
    if (strcmp(req->path, "/api/auth/whoami") == 0) {
        handle_whoami(client, req);
        return 0;
    }
    if (strcmp(req->path, "/api/live/watch") == 0) return handle_live_watch(client);
    if (strcmp(req->path, "/api/live/broadcast") == 0) {
        handle_live_broadcast(client, req);
        return 0;
    }
    if (strcmp(req->path, "/api/chat/stream") == 0) return handle_chat_stream(client);
    if (strcmp(req->path, "/api/chat/send") == 0) {
        handle_chat_send(client, req);
        return 0;
    }
    if (strncmp(req->path, "/videos/", 8) == 0) {
        char name[512];
        snprintf(name, sizeof(name), "%s", req->path + 8);
        url_decode(name);
        serve_video_file(client, req, name);
        return 0;
    }
    if (strcmp(req->path, "/") == 0) {
        char f[1024];
        snprintf(f, sizeof(f), "%s/index.html", g_static_dir);
        serve_static_file(client, req, f);
        return 0;
    }
    if (strcmp(req->path, "/broadcaster.html") == 0) {
        char username[64];
        if (!session_user_from_request(req, username, sizeof(username), NULL, 0)) {
            send_redirect(client, "/login.html");
            return 0;
        }
    }
    const char *allowed[] = {
        "/login.html", "/register.html", "/videos.html", "/live.html", "/broadcaster.html", "/style.css",
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (strcmp(req->path, allowed[i]) == 0) {
            char f[1024];
            snprintf(f, sizeof(f), "%s/%s", g_static_dir, allowed[i] + 1);
            serve_static_file(client, req, f);
            return 0;
        }
    }
    send_simple_response(client, 404, "Not Found", "text/plain; charset=utf-8", "Not Found");
    return 0;
}

static void *client_thread(void *arg) {
    int client = *(int *)arg;
    free(arg);
    struct HttpRequest req;
    int pr = parse_request(client, &req);
    if (pr == -2) {
        send_simple_response(client, 413, "Payload Too Large", "text/plain; charset=utf-8", "Request too large");
        close(client);
        return NULL;
    }
    if (pr != 0) {
        close(client);
        return NULL;
    }
    int handled_close = process_request(client, &req);
    free_request(&req);
    if (!handled_close) close(client);
    return NULL;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    int port = DEFAULT_PORT;
    if (argc >= 2) snprintf(g_video_dir, sizeof(g_video_dir), "%s", argv[1]);
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0) port = DEFAULT_PORT;
    }
    ensure_users_db();
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket");
        return 1;
    }
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server);
        return 1;
    }
    if (listen(server, BACKLOG) < 0) {
        perror("listen");
        close(server);
        return 1;
    }
    printf("Live streaming server listening on :%d\n", port);
    printf("Video directory: %s\n", g_video_dir);
    while (1) {
        int *client = malloc(sizeof(int));
        if (!client) continue;
        *client = accept(server, NULL, NULL);
        if (*client < 0) {
            free(client);
            continue;
        }
        pthread_t t;
        if (pthread_create(&t, NULL, client_thread, client) == 0) pthread_detach(t);
        else {
            close(*client);
            free(client);
        }
    }
    close(server);
    return 0;
}
