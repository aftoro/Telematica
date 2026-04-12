#include "network.h"

#include "../config.h"
#include "../game/game.h"
#include "../protocol/protocol.h"
#include "../utils/logger.h"
#include "client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static ClientConnection g_clients[MAX_PLAYERS];
static pthread_mutex_t g_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static int recv_line(int socket, char *out, size_t out_size) {
    size_t pos = 0;
    int truncated = 0;
    while (pos + 1 < out_size) {
        char c = '\0';
        ssize_t n = recv(socket, &c, 1, 0);
        if (n <= 0) {
            return -1;
        }
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }
        out[pos++] = c;
    }
    out[pos] = '\0';

    // If we filled the buffer without finding \n, drain the rest of the line
    if (pos + 1 >= out_size) {
        truncated = 1;
        char drain[1];
        while (recv(socket, drain, 1, 0) > 0) {
            if (drain[0] == '\n') {
                break;
            }
        }
    }

    return truncated ? -2 : (int)pos;
}

int network_send_to_socket(int socket, const char *line) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s\n", line);
    size_t len = strlen(buffer);
    return network_send_all(socket, buffer, len) == 0 ? 0 : -1;
}

static int network_send_all(int socket, const void *data, size_t len) {
    const char *ptr = (const char *)data;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(socket, ptr + sent, len - sent, 0);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void *client_thread(void *arg) {
    ClientConnection *client = (ClientConnection *)arg;
    char msg[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    client->player_id = game_add_player(client->socket, client->ip, client->port);
    if (client->player_id < 0) {
        network_send_to_socket(client->socket, "ERROR SERVER_FULL");
        close(client->socket);
        client->active = 0;
        return NULL;
    }

    network_send_to_socket(client->socket, "OK CONNECTED");

    while (1) {
        int n = recv_line(client->socket, msg, sizeof(msg));
        if (n < 0) {
            if (n == -2) {
                logger_client(client->ip, client->port, "LINE_TOO_LONG");
            }
            break;
        }

        if (strlen(msg) == 0) {
            continue;
        }

        memset(response, 0, sizeof(response));
        protocol_process_command(client, msg, response, sizeof(response));
        if (strlen(response) > 0) {
            network_send_to_socket(client->socket, response);
        }
    }

    game_remove_player(client->player_id);
    close(client->socket);

    pthread_mutex_lock(&g_clients_mutex);
    client->active = 0;
    pthread_mutex_unlock(&g_clients_mutex);

    logger_server("Cliente desconectado");

    // Log active connections metric
    int active_count = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (g_clients[i].active) active_count++;
    }
    char metric[64];
    snprintf(metric, sizeof(metric), "active_connections=%d", active_count);
    logger_metrics(metric);

    return NULL;
}

int network_start_server(int port) {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        return -1;
    }

    if (listen(server_socket, 16) < 0) {
        perror("listen");
        close(server_socket);
        return -1;
    }

    char log_line[128];
    snprintf(log_line, sizeof(log_line), "Servidor TCP escuchando en %d", port);
    logger_server(log_line);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        // Set socket timeouts
        struct timeval tv;
        tv.tv_sec = 30;  // 30 seconds timeout
        tv.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        pthread_mutex_lock(&g_clients_mutex);
        int slot = -1;
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (!g_clients[i].active) {
                slot = i;
                g_clients[i].active = 1;
                g_clients[i].socket = client_socket;
                g_clients[i].port = ntohs(client_addr.sin_port);
                inet_ntop(AF_INET, &client_addr.sin_addr, g_clients[i].ip, sizeof(g_clients[i].ip));
                break;
            }
        }
        pthread_mutex_unlock(&g_clients_mutex);

        if (slot < 0) {
            network_send_to_socket(client_socket, "ERROR SERVER_FULL");
            close(client_socket);
            continue;
        }

        char c_log[128];
        snprintf(c_log, sizeof(c_log), "Cliente conectado %s:%d", g_clients[slot].ip, g_clients[slot].port);
        logger_server(c_log);

        // Log active connections metric
        int active_count = 0;
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (g_clients[i].active) active_count++;
        }
        char metric[64];
        snprintf(metric, sizeof(metric), "active_connections=%d", active_count);
        logger_metrics(metric);

        if (pthread_create(&g_clients[slot].thread, NULL, client_thread, &g_clients[slot]) != 0) {
            close(client_socket);
            pthread_mutex_lock(&g_clients_mutex);
            g_clients[slot].active = 0;
            pthread_mutex_unlock(&g_clients_mutex);
            continue;
        }
        pthread_detach(g_clients[slot].thread);
    }

    close(server_socket);
    return 0;
}
