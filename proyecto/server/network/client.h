#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include <pthread.h>

typedef struct {
    int socket;
    int port;
    int player_id;
    int active;
    char ip[64];
    pthread_t thread;
} ClientConnection;

#endif
