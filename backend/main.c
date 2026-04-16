#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8085
#define BUFFER 65536
#define VIDEO_DIR "./../db/videos"

// ===================== UTIL =====================
void send_all(int fd, char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) return;
        sent += n;
    }
}

// ===================== HTML =====================
char *HOME_PAGE =
"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
"<h1>Streaming Server</h1>"
"<a href='/videos'>View Videos</a><br>"
"<a href='/live'>Live Stream</a>";


// ===================== VIDEO LIST =====================
void list_videos(int client) {
    DIR *dir = opendir(VIDEO_DIR);

    char res[10000] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
    strcat(res, "<h1>Videos</h1>");

    struct dirent *e;
    while ((e = readdir(dir))) {
        if (strstr(e->d_name, ".mp4")) {
            strcat(res, "<a href='/video/");
            strcat(res, e->d_name);
            strcat(res, "'>");
            strcat(res, e->d_name);
            strcat(res, "</a><br>");
        }
    }

    closedir(dir);
    send_all(client, res, strlen(res));
}

// ===================== RANGE STREAM =====================
void stream_video(int client, char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        send_all(client, "HTTP/1.1 404\r\n\r\n", 18);
        return;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    char header[512];
    sprintf(header,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: video/mp4\r\n"
        "Content-Length: %ld\r\n\r\n",
        size);

    send_all(client, header, strlen(header));

    char buf[BUFFER];
    int n;
    while ((n = read(fd, buf, BUFFER)) > 0) {
        send_all(client, buf, n);
    }

    close(fd);
}

// ===================== LIVE STREAM =====================
#define LIVE_BUF 1024*1024
char live_buffer[LIVE_BUF];
int live_size = 0;
pthread_mutex_t live_lock = PTHREAD_MUTEX_INITIALIZER;

void handle_broadcast(int client) {
    char buf[BUFFER];
    int n;

    while ((n = recv(client, buf, BUFFER, 0)) > 0) {
        pthread_mutex_lock(&live_lock);
        if (live_size + n < LIVE_BUF) {
            memcpy(live_buffer + live_size, buf, n);
            live_size += n;
        }
        pthread_mutex_unlock(&live_lock);
    }

    close(client);
}

void handle_live_view(int client) {
    send_all(client,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: video/webm\r\n"
        "Transfer-Encoding: chunked\r\n\r\n", 90);

    while (1) {
        pthread_mutex_lock(&live_lock);

        if (live_size > 0) {
            char chunk_header[32];
            sprintf(chunk_header, "%x\r\n", live_size);
            send_all(client, chunk_header, strlen(chunk_header));
            send_all(client, live_buffer, live_size);
            send_all(client, "\r\n", 2);

            live_size = 0;
        }

        pthread_mutex_unlock(&live_lock);
        usleep(100000);
    }
}

// ===================== ROUTER =====================
void *handle_client(void *arg) {
    int client = *(int *)arg;
    free(arg);

    char req[2048] = {0};
    read(client, req, sizeof(req));

    char method[10], path[512];
    sscanf(req, "%s %s", method, path);

    printf("REQ: %s\n", path);

    if (strcmp(path, "/") == 0) {
        send_all(client, HOME_PAGE, strlen(HOME_PAGE));
    }
    else if (strcmp(path, "/videos") == 0) {
        list_videos(client);
    }
    else if (strncmp(path, "/video/", 7) == 0) {
        char full[512];
        sprintf(full, "%s/%s", VIDEO_DIR, path + 7);
        stream_video(client, full);
    }
    else if (strcmp(path, "/live") == 0) {
        handle_live_view(client);
    }
    else if (strcmp(path, "/broadcast") == 0) {
        handle_broadcast(client);
    }
    else {
        send_all(client, "HTTP/1.1 404\r\n\r\n", 18);
    }

    close(client);
    return NULL;
}

// ===================== MAIN =====================
int main() {
    int server = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bind(server, (struct sockaddr *)&addr, sizeof(addr));
    listen(server, 10);

    printf("Server running on %d\n", PORT);

    while (1) {
        int *client = malloc(sizeof(int));
        *client = accept(server, NULL, NULL);

        pthread_t t;
        pthread_create(&t, NULL, handle_client, client);
        pthread_detach(t);
    }
}