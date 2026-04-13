#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>

#define HTTP_BUFFER 8192
#define MAX_SESSIONS 64
#define MAX_ALERTS 32
#define BACKEND_LINE_BUFFER 2048

typedef struct {
    int used;
    char token[32];
    char name[32];
    char role[16];
    char room[32];
    int backend_socket;
    char alerts[MAX_ALERTS][128];
    int alert_count;
} Session;

static Session g_sessions[MAX_SESSIONS];

static int recv_line_timeout(int sock, char *out, size_t out_size, int timeout_ms) {
    size_t pos = 0;
    while (1) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock, &set);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ready = select(sock + 1, &set, NULL, NULL, &tv);
        if (ready <= 0) {
            return -1;
        }

        char c;
        ssize_t n = recv(sock, &c, 1, 0);
        if (n <= 0) {
            return -1;
        }
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }

        // If a backend line is longer than buffer, keep draining until '\n'
        // to preserve stream alignment for next command.
        if (pos + 1 < out_size) {
            out[pos++] = c;
        }
    }
    out[pos] = '\0';
    return (int)pos;
}

static void push_alert(Session *session, const char *line) {
    if (session->alert_count >= MAX_ALERTS) {
        for (int i = 1; i < MAX_ALERTS; ++i) {
            strncpy(session->alerts[i - 1], session->alerts[i], sizeof(session->alerts[i - 1]) - 1);
            session->alerts[i - 1][sizeof(session->alerts[i - 1]) - 1] = '\0';
        }
        session->alert_count = MAX_ALERTS - 1;
    }

    strncpy(session->alerts[session->alert_count], line, sizeof(session->alerts[session->alert_count]) - 1);
    session->alerts[session->alert_count][sizeof(session->alerts[session->alert_count]) - 1] = '\0';
    session->alert_count++;
}

static void drain_backend_alerts(Session *session) {
    if (session->backend_socket <= 0) {
        return;
    }

    while (1) {
        char line[256];
        int n = recv_line_timeout(session->backend_socket, line, sizeof(line), 10);
        if (n <= 0) {
            break;
        }
        if (strncmp(line, "ALERT", 5) == 0) {
            push_alert(session, line);
        }
    }
}

static int send_http(int client, const char *status, const char *content_type, const char *body) {
    char header[512];
    size_t body_len = strlen(body);
    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n",
             status, content_type, body_len);

    if (send(client, header, strlen(header), 0) < 0) {
        return -1;
    }
    if (send(client, body, body_len, 0) < 0) {
        return -1;
    }
    return 0;
}

static int send_file(int client, const char *path, const char *content_type) {
    char full1[256];
    char full2[256];
    snprintf(full1, sizeof(full1), "frontend/%s", path);
    snprintf(full2, sizeof(full2), "../frontend/%s", path);

    const char *chosen = NULL;
    FILE *f = fopen(full1, "rb");
    if (f != NULL) {
        chosen = full1;
    } else {
        f = fopen(full2, "rb");
        if (f != NULL) {
            chosen = full2;
        }
    }

    if (f == NULL || chosen == NULL) {
        return send_http(client, "404 Not Found", "text/plain", "Not Found\n");
    }

    (void)chosen;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0 || size > 1024 * 1024) {
        fclose(f);
        return send_http(client, "500 Internal Server Error", "text/plain", "File Error\n");
    }

    char *data = (char *)malloc((size_t)size + 1);
    if (data == NULL) {
        fclose(f);
        return send_http(client, "500 Internal Server Error", "text/plain", "Memory Error\n");
    }

    fread(data, 1, (size_t)size, f);
    fclose(f);
    data[size] = '\0';

    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n\r\n",
             content_type, size);

    send(client, header, strlen(header), 0);
    send(client, data, (size_t)size, 0);
    free(data);
    return 0;
}

static void url_decode(char *dst, const char *src, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_size; ++i) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
            char hex[3] = {src[i + 1], src[i + 2], '\0'};
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static int get_query_param(const char *query, const char *key, char *out, size_t out_size) {
    if (query == NULL) {
        return -1;
    }

    char copy[1024];
    strncpy(copy, query, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *token = strtok(copy, "&");
    while (token != NULL) {
        char *eq = strchr(token, '=');
        if (eq != NULL) {
            *eq = '\0';
            if (strcmp(token, key) == 0) {
                url_decode(out, eq + 1, out_size);
                return 0;
            }
        }
        token = strtok(NULL, "&");
    }

    return -1;
}

static Session *find_session(const char *token) {
    for (int i = 0; i < MAX_SESSIONS; ++i) {
        if (g_sessions[i].used && strcmp(g_sessions[i].token, token) == 0) {
            return &g_sessions[i];
        }
    }
    return NULL;
}

static Session *create_session(const char *name, const char *role) {
    for (int i = 0; i < MAX_SESSIONS; ++i) {
        if (!g_sessions[i].used) {
            g_sessions[i].used = 1;
            g_sessions[i].backend_socket = -1;
            g_sessions[i].alert_count = 0;
            snprintf(g_sessions[i].token, sizeof(g_sessions[i].token), "tok_%d_%d", i, rand() % 100000);
            strncpy(g_sessions[i].name, name, sizeof(g_sessions[i].name) - 1);
            g_sessions[i].name[sizeof(g_sessions[i].name) - 1] = '\0';
            strncpy(g_sessions[i].role, role, sizeof(g_sessions[i].role) - 1);
            g_sessions[i].role[sizeof(g_sessions[i].role) - 1] = '\0';
            strcpy(g_sessions[i].room, "-");
            return &g_sessions[i];
        }
    }
    return NULL;
}

static int connect_backend(const char *host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }
}

struct addrinfo hints, *res;

memset(&hints, 0, sizeof(hints));
hints.ai_family = AF_INET;
hints.ai_socktype = SOCK_STREAM;

char port_str[10];
sprintf(port_str, "%d", port);

if (getaddrinfo(host, port_str, &hints, &res) != 0) {
    close(sock);
    return -1;
}

if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
    freeaddrinfo(res);
    close(sock);
    return -1;
}

freeaddrinfo(res);  

static void handle_api(int client, const char *path, const char *query) {
    if (strcmp(path, "/api/lobby") == 0) {
        send_http(client, "200 OK", "application/json",
                  "{\"rooms\":[{\"id\":\"room1\",\"name\":\"Sala Alpha\"},{\"id\":\"room2\",\"name\":\"Sala Sigma\"}]}\n");
        return;
    }

    if (strcmp(path, "/api/login") == 0) {
        char name[32];
        char role[16];
        if (get_query_param(query, "name", name, sizeof(name)) != 0 ||
            get_query_param(query, "role", role, sizeof(role)) != 0) {
            send_http(client, "400 Bad Request", "text/plain", "Missing params\n");
            return;
        }

        if (strcmp(role, "ATACANTE") != 0 && strcmp(role, "DEFENSOR") != 0) {
            send_http(client, "400 Bad Request", "text/plain", "Invalid role\n");
            return;
        }

        Session *session = create_session(name, role);
        if (session == NULL) {
            send_http(client, "503 Service Unavailable", "text/plain", "No session slots\n");
            return;
        }

        char body[256];
        snprintf(body, sizeof(body), "{\"token\":\"%s\",\"name\":\"%s\",\"role\":\"%s\"}\n",
                 session->token, session->name, session->role);
        send_http(client, "200 OK", "application/json", body);
        return;
    }

    if (strcmp(path, "/api/join") == 0) {
        char token[32];
        char room[32];
        if (get_query_param(query, "token", token, sizeof(token)) != 0 ||
            get_query_param(query, "room", room, sizeof(room)) != 0) {
            send_http(client, "400 Bad Request", "text/plain", "Missing params\n");
            return;
        }

        Session *session = find_session(token);
        if (session == NULL) {
            send_http(client, "404 Not Found", "text/plain", "Invalid token\n");
            return;
        }

        if (session->backend_socket <= 0) {
            session->backend_socket = connect_backend("127.0.0.1", 9090);
            if (session->backend_socket <= 0) {
                send_http(client, "503 Service Unavailable", "text/plain", "Backend down\n");
                return;
            }

            char hello[128];
            char line[256];
            recv_line_timeout(session->backend_socket, line, sizeof(line), 500);

            snprintf(hello, sizeof(hello), "ROLE %s %s\n", session->role, session->name);
            send(session->backend_socket, hello, strlen(hello), 0);
            if (recv_line_timeout(session->backend_socket, line, sizeof(line), 500) <= 0) {
                send_http(client, "500 Internal Server Error", "text/plain", "Role setup failed\n");
                return;
            }
        }

        strncpy(session->room, room, sizeof(session->room) - 1);
        session->room[sizeof(session->room) - 1] = '\0';
        send_http(client, "200 OK", "application/json", "{\"ok\":true}\n");
        return;
    }

    if (strcmp(path, "/api/command") == 0) {
        char token[32];
        char cmd[256];
        if (get_query_param(query, "token", token, sizeof(token)) != 0 ||
            get_query_param(query, "cmd", cmd, sizeof(cmd)) != 0) {
            send_http(client, "400 Bad Request", "text/plain", "Missing params\n");
            return;
        }

        Session *session = find_session(token);
        if (session == NULL || session->backend_socket <= 0) {
            send_http(client, "403 Forbidden", "text/plain", "Not joined\n");
            return;
        }

        drain_backend_alerts(session);

        char to_send[300];
        snprintf(to_send, sizeof(to_send), "%s\n", cmd);
        if (send(session->backend_socket, to_send, strlen(to_send), 0) < 0) {
            send_http(client, "500 Internal Server Error", "text/plain", "Send error\n");
            return;
        }

        char line[BACKEND_LINE_BUFFER];
        while (1) {
            int n = recv_line_timeout(session->backend_socket, line, sizeof(line), 1000);
            if (n <= 0) {
                send_http(client, "504 Gateway Timeout", "text/plain", "Backend timeout\n");
                return;
            }

            if (strncmp(line, "ALERT", 5) == 0) {
                push_alert(session, line);
                continue;
            }

            send_http(client, "200 OK", "text/plain", line);
            return;
        }
    }

    if (strcmp(path, "/api/alerts") == 0) {
        char token[32];
        if (get_query_param(query, "token", token, sizeof(token)) != 0) {
            send_http(client, "400 Bad Request", "text/plain", "Missing token\n");
            return;
        }

        Session *session = find_session(token);
        if (session == NULL) {
            send_http(client, "404 Not Found", "text/plain", "Invalid token\n");
            return;
        }

        drain_backend_alerts(session);

        char body[2048];
        body[0] = '\0';
        if (session->alert_count == 0) {
            strcpy(body, "NONE");
        } else {
            for (int i = 0; i < session->alert_count; ++i) {
                if (i > 0) {
                    strncat(body, "|", sizeof(body) - strlen(body) - 1);
                }
                strncat(body, session->alerts[i], sizeof(body) - strlen(body) - 1);
            }
            session->alert_count = 0;
        }

        send_http(client, "200 OK", "text/plain", body);
        return;
    }

    send_http(client, "404 Not Found", "text/plain", "Unknown API\n");
}

static void handle_client(int client) {
    char req[HTTP_BUFFER];
    int n = recv(client, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        close(client);
        return;
    }
    req[n] = '\0';

    char method[8];
    char target[512];
    if (sscanf(req, "%7s %511s", method, target) != 2) {
        send_http(client, "400 Bad Request", "text/plain", "Bad request\n");
        close(client);
        return;
    }

    if (strcmp(method, "GET") != 0) {
        send_http(client, "405 Method Not Allowed", "text/plain", "Only GET\n");
        close(client);
        return;
    }

    char path[512];
    char *query = NULL;
    strncpy(path, target, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    char *qm = strchr(path, '?');
    if (qm != NULL) {
        *qm = '\0';
        query = qm + 1;
    }

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        send_file(client, "index.html", "text/html; charset=utf-8");
    } else if (strcmp(path, "/style.css") == 0) {
        send_file(client, "style.css", "text/css; charset=utf-8");
    } else if (strcmp(path, "/game.js") == 0) {
        send_file(client, "game.js", "application/javascript; charset=utf-8");
    } else if (strncmp(path, "/api/", 5) == 0) {
        handle_api(client, path, query);
    } else {
        send_http(client, "404 Not Found", "text/plain", "Not Found\n");
    }

    close(client);
}

int main(int argc, char *argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    srand((unsigned int)time(NULL));

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
    addr.sin_port = htons(port);

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server);
        return 1;
    }

    if (listen(server, 16) < 0) {
        perror("listen");
        close(server);
        return 1;
    }

    printf("HTTP server en puerto %d\n", port);

    while (1) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int client = accept(server, (struct sockaddr *)&cli, &len);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }
        handle_client(client);
    }

    close(server);
    return 0;
}
