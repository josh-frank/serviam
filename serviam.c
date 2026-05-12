/*
 * serviam - world's smallest web server
 *
 * Usage: serviam <port> [root_dir]
 *   serviam 8080
 *   serviam 8080 /var/www
 *
 * - No dependencies beyond POSIX
 * - Compile: cc serviam.c -o serviam
 * - Serves static files only (GET)
 * - Fork per connection (Pi-appropriate concurrency)
 * - Path traversal protection
 * - Returns 200, 404, 403, 405
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE    4096
#define MAX_PATH    1024
#define MAX_HEADERS 8192
#define BACKLOG     10

/* ── MIME types ─────────────────────────────────────────────────────────── */

static const struct { const char *ext, *type; } MIME[] = {
    { ".html", "text/html" },
    { ".css",  "text/css" },
    { ".js",   "application/javascript" },
    { ".json", "application/json" },
    { ".png",  "image/png" },
    { ".jpg",  "image/jpeg" },
    { ".jpeg", "image/jpeg" },
    { ".gif",  "image/gif" },
    { ".svg",  "image/svg+xml" },
    { ".ico",  "image/x-icon" },
    { ".txt",  "text/plain" },
    { NULL, NULL }
};

static const char *mime_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    for (int i = 0; MIME[i].ext; i++)
        if (strcmp(dot, MIME[i].ext) == 0)
            return MIME[i].type;
    return "application/octet-stream";
}

/* ── Response helpers ───────────────────────────────────────────────────── */

static void send_status(int fd, int code, const char *msg, const char *body) {
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        code, msg, strlen(body), body);
    write(fd, buf, n);
}

static void serve_file(int fd, const char *path) {
    struct stat st;
    if (stat(path, &st) < 0)          { send_status(fd, 404, "Not Found",   "404 Not Found\n");   return; }
    if (!S_ISREG(st.st_mode))         { send_status(fd, 403, "Forbidden",   "403 Forbidden\n");   return; }

    int file = open(path, O_RDONLY);
    if (file < 0)                      { send_status(fd, 403, "Forbidden",   "403 Forbidden\n");   return; }

    /* Headers */
    char headers[MAX_HEADERS];
    int hlen = snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime_for(path), (long long)st.st_size);
    write(fd, headers, hlen);

    /* Body */
    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(file, buf, sizeof(buf))) > 0)
        write(fd, buf, n);

    close(file);
}

/* ── Request handling (runs in child) ──────────────────────────────────── */

static void handle(int fd, const char *root) {
    char buf[BUF_SIZE];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';

    /* Parse request line: METHOD /path HTTP/x.x */
    char method[8], raw_path[MAX_PATH], proto[16];
    if (sscanf(buf, "%7s %1023s %15s", method, raw_path, proto) != 3) {
        send_status(fd, 400, "Bad Request", "400 Bad Request\n");
        return;
    }

    /* Only GET */
    if (strcmp(method, "GET") != 0) {
        send_status(fd, 405, "Method Not Allowed", "405 Method Not Allowed\n");
        return;
    }

    /* ── Path traversal protection ──────────────────────────────────────
     * Reject anything containing ".." so callers can't escape root.
     * We check the raw path before any filesystem access.
     * ─────────────────────────────────────────────────────────────────── */
    if (strstr(raw_path, "..")) {
        send_status(fd, 403, "Forbidden", "403 Forbidden\n");
        return;
    }

    /* Strip query string */
    char *q = strchr(raw_path, '?');
    if (q) *q = '\0';

    /* Build full filesystem path */
    char full_path[MAX_PATH * 2];
    snprintf(full_path, sizeof(full_path), "%s%s", root, raw_path);

    /* Directory → try index.html */
    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        char index[MAX_PATH * 2 + 12];
        snprintf(index, sizeof(index), "%s/index.html",
                 full_path[strlen(full_path) - 1] == '/'
                     ? (full_path[strlen(full_path) - 1] = '\0', full_path)
                     : full_path);
        serve_file(fd, index);
        return;
    }

    serve_file(fd, full_path);
}

/* ── SIGCHLD: reap zombies ──────────────────────────────────────────────── */

static void reap(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port> [root_dir]\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    const char *root = argc >= 3 ? argv[2] : ".";

    /* Validate root exists */
    struct stat rst;
    if (stat(root, &rst) < 0 || !S_ISDIR(rst.st_mode)) {
        fprintf(stderr, "serviam: '%s' is not a directory\n", root);
        return 1;
    }

    /* Socket setup */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(server_fd, BACKLOG) < 0)                               { perror("listen"); return 1; }

    signal(SIGCHLD, reap);  /* reap zombie children automatically */

    printf("serviam listening on :%d, serving '%s'\n", port, root);

    /* ── Main accept loop ───────────────────────────────────────────────── */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int conn = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (conn < 0) continue;  /* interrupted by SIGCHLD, loop again */

        printf("%s:%d → %s\n",
            inet_ntoa(client_addr.sin_addr),
            ntohs(client_addr.sin_port),
            "connected");

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(conn);
            continue;
        }

        if (pid == 0) {
            /* Child: handle request and die */
            close(server_fd);   /* child doesn't need the listener */
            handle(conn, root);
            close(conn);
            exit(0);
        }

        /* Parent: not our job to serve this connection */
        close(conn);
    }

    return 0;  /* unreachable */
}
