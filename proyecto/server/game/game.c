#include "game.h"

#include "../config.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    RESOURCE_NORMAL = 0,
    RESOURCE_ATTACKED = 1,
    RESOURCE_COMPROMISED = 2
} ResourceState;

typedef struct {
    int id;
    int socket;
    int port;
    int active;
    int x;
    int y;
    int points;
    char ip[64];
    char name[32];
    PlayerRole role;
    int discovered[MAX_RESOURCES];
} Player;

typedef struct {
    int id;
    int x;
    int y;
    char name[32];
    ResourceState state;
    time_t attacked_at;
    int attacker_id;
} Resource;

static Player g_players[MAX_PLAYERS];
static Resource g_resources[MAX_RESOURCES];
static int g_resource_count = 0;
static pthread_mutex_t g_game_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_seeded = 0;

static int find_player_index_by_id(int player_id);

static const char *role_to_text(PlayerRole role) {
    return role == ROLE_DEFENDER ? "DEFENSOR" : "ATACANTE";
}

static int random_coord(void) {
    return rand() % (MAP_WIDTH + 1);
}

static void relocate_resource_locked(Resource *resource) {
    resource->x = random_coord();
    resource->y = random_coord();
    resource->state = RESOURCE_NORMAL;
    resource->attacked_at = 0;
    resource->attacker_id = -1;
}

static void award_points_locked(int player_id, int points) {
    int idx = find_player_index_by_id(player_id);
    if (idx >= 0) {
        g_players[idx].points += points;
    }
}

static void update_attack_windows_locked(void) {
    time_t now = time(NULL);
    for (int i = 0; i < g_resource_count; ++i) {
        if (g_resources[i].state == RESOURCE_ATTACKED) {
            if ((int)(now - g_resources[i].attacked_at) > ATTACK_WINDOW_SECONDS) {
                if (g_resources[i].attacker_id >= 0) {
                    award_points_locked(g_resources[i].attacker_id, 10);
                }
                g_resources[i].state = RESOURCE_COMPROMISED;
                g_resources[i].attacked_at = 0;
            }
        }
    }
}

static int find_resource_index_by_id(int resource_id) {
    for (int i = 0; i < g_resource_count; ++i) {
        if (g_resources[i].id == resource_id) {
            return i;
        }
    }
    return -1;
}

static int find_player_index_by_id(int player_id) {
    if (player_id < 0 || player_id >= MAX_PLAYERS) {
        return -1;
    }
    if (!g_players[player_id].active) {
        return -1;
    }
    return player_id;
}

void game_init(void) {
    pthread_mutex_lock(&g_game_mutex);

    if (!g_seeded) {
        srand((unsigned int)time(NULL));
        g_seeded = 1;
    }

    memset(g_players, 0, sizeof(g_players));
    memset(g_resources, 0, sizeof(g_resources));

    g_resource_count = 2;
    g_resources[0].id = 1;
    g_resources[0].x = 30;
    g_resources[0].y = 40;
    strcpy(g_resources[0].name, "server1");
    g_resources[0].state = RESOURCE_NORMAL;
    g_resources[0].attacker_id = -1;

    g_resources[1].id = 2;
    g_resources[1].x = 70;
    g_resources[1].y = 80;
    strcpy(g_resources[1].name, "server2");
    g_resources[1].state = RESOURCE_NORMAL;
    g_resources[1].attacker_id = -1;

    pthread_mutex_unlock(&g_game_mutex);
}

int game_add_player(int socket, const char *ip, int port) {
    pthread_mutex_lock(&g_game_mutex);

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (!g_players[i].active) {
            g_players[i].id = i;
            g_players[i].socket = socket;
            g_players[i].port = port;
            g_players[i].active = 1;
            g_players[i].x = 0;
            g_players[i].y = 0;
            g_players[i].points = 0;
            g_players[i].role = ROLE_ATTACKER;
            snprintf(g_players[i].name, sizeof(g_players[i].name), "guest%d", i);
            strncpy(g_players[i].ip, ip, sizeof(g_players[i].ip) - 1);
            g_players[i].ip[sizeof(g_players[i].ip) - 1] = '\0';
            memset(g_players[i].discovered, 0, sizeof(g_players[i].discovered));

            pthread_mutex_unlock(&g_game_mutex);
            return i;
        }
    }

    pthread_mutex_unlock(&g_game_mutex);
    return -1;
}

void game_remove_player(int player_id) {
    pthread_mutex_lock(&g_game_mutex);
    int idx = find_player_index_by_id(player_id);
    if (idx >= 0) {
        memset(&g_players[idx], 0, sizeof(Player));
    }
    pthread_mutex_unlock(&g_game_mutex);
}

int game_set_role_and_name(int player_id, const char *role_text, const char *name) {
    pthread_mutex_lock(&g_game_mutex);
    int idx = find_player_index_by_id(player_id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_game_mutex);
        return -1;
    }

    if (strcmp(role_text, "DEFENSOR") == 0) {
        g_players[idx].role = ROLE_DEFENDER;
    } else if (strcmp(role_text, "ATACANTE") == 0) {
        g_players[idx].role = ROLE_ATTACKER;
    } else {
        pthread_mutex_unlock(&g_game_mutex);
        return -2;
    }

    if (name != NULL && name[0] != '\0') {
        strncpy(g_players[idx].name, name, sizeof(g_players[idx].name) - 1);
        g_players[idx].name[sizeof(g_players[idx].name) - 1] = '\0';
    }

    if (g_players[idx].role == ROLE_DEFENDER) {
        for (int i = 0; i < g_resource_count; ++i) {
            g_players[idx].discovered[i] = 1;
        }
    }

    pthread_mutex_unlock(&g_game_mutex);
    return 0;
}

PlayerRole game_get_role(int player_id) {
    pthread_mutex_lock(&g_game_mutex);
    int idx = find_player_index_by_id(player_id);
    PlayerRole role = ROLE_ATTACKER;
    if (idx >= 0) {
        role = g_players[idx].role;
    }
    pthread_mutex_unlock(&g_game_mutex);
    return role;
}

int game_move_player(int player_id, int x, int y) {
    pthread_mutex_lock(&g_game_mutex);
    int idx = find_player_index_by_id(player_id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_game_mutex);
        return -1;
    }

    if (x < 0 || y < 0 || x > MAP_WIDTH || y > MAP_HEIGHT) {
        pthread_mutex_unlock(&g_game_mutex);
        return -2;
    }

    g_players[idx].x = x;
    g_players[idx].y = y;

    pthread_mutex_unlock(&g_game_mutex);
    return 0;
}

int game_scan_resources(int player_id, char *out, size_t out_size) {
    pthread_mutex_lock(&g_game_mutex);
    int idx = find_player_index_by_id(player_id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_game_mutex);
        return -1;
    }

    update_attack_windows_locked();

    out[0] = '\0';
    int found = 0;

    for (int i = 0; i < g_resource_count; ++i) {
        int dx = abs(g_players[idx].x - g_resources[i].x);
        int dy = abs(g_players[idx].y - g_resources[i].y);

        if (dx <= 8 && dy <= 8) {
            g_players[idx].discovered[i] = 1;
            char chunk[96];
            snprintf(chunk, sizeof(chunk), "%d:%s:%d:%d:%d", g_resources[i].id, g_resources[i].name,
                     g_resources[i].x, g_resources[i].y, (int)g_resources[i].state);

            if (found > 0) {
                strncat(out, ",", out_size - strlen(out) - 1);
            }
            strncat(out, chunk, out_size - strlen(out) - 1);
            found++;
        }
    }

    pthread_mutex_unlock(&g_game_mutex);
    return found;
}

int game_attack_resource(int player_id, int resource_id, int *alert_resource_id) {
    pthread_mutex_lock(&g_game_mutex);
    int pidx = find_player_index_by_id(player_id);
    if (pidx < 0) {
        pthread_mutex_unlock(&g_game_mutex);
        return -1;
    }
    if (g_players[pidx].role != ROLE_ATTACKER) {
        pthread_mutex_unlock(&g_game_mutex);
        return -2;
    }

    int ridx = find_resource_index_by_id(resource_id);
    if (ridx < 0) {
        pthread_mutex_unlock(&g_game_mutex);
        return -3;
    }

    int dx = abs(g_players[pidx].x - g_resources[ridx].x);
    int dy = abs(g_players[pidx].y - g_resources[ridx].y);
    if (dx > 10 || dy > 10) {
        pthread_mutex_unlock(&g_game_mutex);
        return -4;
    }

    update_attack_windows_locked();

    if (g_resources[ridx].state == RESOURCE_ATTACKED) {
        pthread_mutex_unlock(&g_game_mutex);
        return -5;
    }

    g_resources[ridx].state = RESOURCE_ATTACKED;
    g_resources[ridx].attacked_at = time(NULL);
    g_resources[ridx].attacker_id = pidx;

    if (alert_resource_id != NULL) {
        *alert_resource_id = g_resources[ridx].id;
    }

    pthread_mutex_unlock(&g_game_mutex);
    return 0;
}

int game_mitigate_resource(int player_id, int resource_id, int *new_x, int *new_y) {
    pthread_mutex_lock(&g_game_mutex);
    int pidx = find_player_index_by_id(player_id);
    if (pidx < 0) {
        pthread_mutex_unlock(&g_game_mutex);
        return -1;
    }
    if (g_players[pidx].role != ROLE_DEFENDER) {
        pthread_mutex_unlock(&g_game_mutex);
        return -2;
    }

    int ridx = find_resource_index_by_id(resource_id);
    if (ridx < 0) {
        pthread_mutex_unlock(&g_game_mutex);
        return -3;
    }

    update_attack_windows_locked();

    if (g_resources[ridx].state == RESOURCE_COMPROMISED) {
        pthread_mutex_unlock(&g_game_mutex);
        return -5;
    }

    if (g_resources[ridx].state != RESOURCE_ATTACKED) {
        pthread_mutex_unlock(&g_game_mutex);
        return -4;
    }

    g_players[pidx].points += 5;
    relocate_resource_locked(&g_resources[ridx]);

    if (new_x != NULL) {
        *new_x = g_resources[ridx].x;
    }
    if (new_y != NULL) {
        *new_y = g_resources[ridx].y;
    }

    pthread_mutex_unlock(&g_game_mutex);
    return 0;
}

int game_get_resource_state(int resource_id, int *x, int *y, int *state) {
    pthread_mutex_lock(&g_game_mutex);
    int ridx = find_resource_index_by_id(resource_id);
    if (ridx < 0) {
        pthread_mutex_unlock(&g_game_mutex);
        return -1;
    }

    if (x != NULL) {
        *x = g_resources[ridx].x;
    }
    if (y != NULL) {
        *y = g_resources[ridx].y;
    }
    if (state != NULL) {
        *state = g_resources[ridx].state;
    }

    pthread_mutex_unlock(&g_game_mutex);
    return 0;
}

int game_status(int player_id, char *out, size_t out_size) {
    pthread_mutex_lock(&g_game_mutex);
    int pidx = find_player_index_by_id(player_id);
    if (pidx < 0) {
        pthread_mutex_unlock(&g_game_mutex);
        return -1;
    }

    update_attack_windows_locked();

    snprintf(out, out_size, "DATA STATUS PLAYERS ");
    int wrote_players = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (!g_players[i].active) {
            continue;
        }
        char chunk[128];
        snprintf(chunk, sizeof(chunk), "%d:%s:%s:%d:%d:%d", g_players[i].id, g_players[i].name,
                 role_to_text(g_players[i].role), g_players[i].x, g_players[i].y, g_players[i].points);
        if (wrote_players > 0) {
            strncat(out, ",", out_size - strlen(out) - 1);
        }
        strncat(out, chunk, out_size - strlen(out) - 1);
        wrote_players++;
    }

    strncat(out, " RESOURCES ", out_size - strlen(out) - 1);
    int wrote_resources = 0;
    for (int i = 0; i < g_resource_count; ++i) {
        int visible = g_players[pidx].role == ROLE_DEFENDER || g_players[pidx].discovered[i];
        if (!visible) {
            continue;
        }
        char chunk[128];
        snprintf(chunk, sizeof(chunk), "%d:%s:%d:%d:%d", g_resources[i].id, g_resources[i].name,
                 g_resources[i].x, g_resources[i].y, (int)g_resources[i].state);
        if (wrote_resources > 0) {
            strncat(out, ",", out_size - strlen(out) - 1);
        }
        strncat(out, chunk, out_size - strlen(out) - 1);
        wrote_resources++;
    }

    pthread_mutex_unlock(&g_game_mutex);
    return 0;
}

int game_collect_defender_sockets(int *out_sockets, int max_sockets) {
    pthread_mutex_lock(&g_game_mutex);
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS && count < max_sockets; ++i) {
        if (g_players[i].active && g_players[i].role == ROLE_DEFENDER) {
            out_sockets[count++] = g_players[i].socket;
        }
    }
    pthread_mutex_unlock(&g_game_mutex);
    return count;
}
