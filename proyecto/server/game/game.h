#ifndef GAME_H
#define GAME_H

#include <stddef.h>

typedef enum {
    ROLE_ATTACKER = 0,
    ROLE_DEFENDER = 1
} PlayerRole;

void game_init(void);
int game_add_player(int socket, const char *ip, int port);
void game_remove_player(int player_id);
int game_set_role_and_name(int player_id, const char *role_text, const char *name);
PlayerRole game_get_role(int player_id);
int game_move_player(int player_id, int x, int y);
int game_scan_resources(int player_id, char *out, size_t out_size);
int game_attack_resource(int player_id, int resource_id, int *alert_resource_id);
int game_mitigate_resource(int player_id, int resource_id, int *new_x, int *new_y);
int game_status(int player_id, char *out, size_t out_size);
int game_collect_defender_sockets(int *out_sockets, int max_sockets);
int game_get_resource_state(int resource_id, int *x, int *y, int *state);

#endif
