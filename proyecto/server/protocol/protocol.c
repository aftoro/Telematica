#include "protocol.h"

#include "../game/game.h"
#include "../network/network.h"
#include "../utils/logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *text, int *value) {
    char *end = NULL;
    long v = strtol(text, &end, 10);
    if (end == NULL || *end != '\0') {
        return -1;
    }
    *value = (int)v;
    return 0;
}

int protocol_process_command(ClientConnection *client, const char *line, char *response, size_t response_size) {
    if (strlen(line) > MAX_MESSAGE_LEN) {
        snprintf(response, response_size, "ERROR MESSAGE_TOO_LONG");
        return 0;
    }

    char buffer[MAX_MESSAGE_LEN + 1];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    logger_client(client->ip, client->port, line);

    char *saveptr = NULL;
    char *cmd = strtok_r(buffer, " ", &saveptr);
    if (cmd == NULL) {
        snprintf(response, response_size, "ERROR EMPTY_COMMAND");
        return 0;
    }

    if (strcmp(cmd, "ROLE") == 0) {
        char *role = strtok_r(NULL, " ", &saveptr);
        char *name = strtok_r(NULL, " ", &saveptr);
        if (role == NULL || name == NULL) {
            snprintf(response, response_size, "ERROR PARAMS");
            return 0;
        }

        int rc = game_set_role_and_name(client->player_id, role, name);
        if (rc == 0) {
            snprintf(response, response_size, "OK ROLE");
        } else {
            logger_metrics("error_command=ROLE");
            snprintf(response, response_size, "ERROR ROLE");
        }
        return 0;
    }

    if (strcmp(cmd, "MOVE") == 0) {
        char *xs = strtok_r(NULL, " ", &saveptr);
        char *ys = strtok_r(NULL, " ", &saveptr);
        int x = 0;
        int y = 0;
        if (xs == NULL || ys == NULL || parse_int(xs, &x) != 0 || parse_int(ys, &y) != 0) {
            snprintf(response, response_size, "ERROR PARAMS");
            return 0;
        }

        int rc = game_move_player(client->player_id, x, y);
        if (rc == 0) {
            snprintf(response, response_size, "OK MOVE");
        } else if (rc == -2) {
            snprintf(response, response_size, "ERROR OUT_OF_BOUNDS");
        } else {
            snprintf(response, response_size, "ERROR MOVE");
        }
        return 0;
    }

    if (strcmp(cmd, "SCAN") == 0) {
        char scan_data[512];
        int found = game_scan_resources(client->player_id, scan_data, sizeof(scan_data));
        if (found < 0) {
            snprintf(response, response_size, "ERROR SCAN");
            return 0;
        }

        if (found == 0) {
            snprintf(response, response_size, "DATA SCAN NONE");
        } else {
            snprintf(response, response_size, "DATA SCAN %s", scan_data);
        }
        return 0;
    }

    if (strcmp(cmd, "ATTACK") == 0) {
        char *rid = strtok_r(NULL, " ", &saveptr);
        int resource_id = 0;
        if (rid == NULL || parse_int(rid, &resource_id) != 0) {
            snprintf(response, response_size, "ERROR PARAMS");
            return 0;
        }

        int alert_resource_id = 0;
        int rc = game_attack_resource(client->player_id, resource_id, &alert_resource_id);
        if (rc == 0) {
            int defender_sockets[128];
            int count = game_collect_defender_sockets(defender_sockets, 128);
            char alert[128];
            snprintf(alert, sizeof(alert), "ALERT %d", alert_resource_id);
            for (int i = 0; i < count; ++i) {
                network_send_to_socket(defender_sockets[i], alert);
            }
            logger_server("ALERT RESOURCE dispatch");
            snprintf(response, response_size, "OK ATTACK %d", resource_id);
        } else if (rc == -2) {
            snprintf(response, response_size, "ERROR ROLE");
        } else if (rc == -3) {
            snprintf(response, response_size, "ERROR NOT_FOUND");
        } else if (rc == -4) {
            snprintf(response, response_size, "ERROR TOO_FAR");
        } else {
            snprintf(response, response_size, "ERROR ATTACK");
        }
        return 0;
    }

    if (strcmp(cmd, "MITIGATE") == 0) {
        char *rid = strtok_r(NULL, " ", &saveptr);
        int resource_id = 0;
        if (rid == NULL || parse_int(rid, &resource_id) != 0) {
            snprintf(response, response_size, "ERROR PARAMS");
            return 0;
        }

        int new_x = 0;
        int new_y = 0;
        int rc = game_mitigate_resource(client->player_id, resource_id, &new_x, &new_y);
        if (rc == 0) {
            snprintf(response, response_size, "OK MITIGATE %d RELOCATED %d %d", resource_id, new_x, new_y);
        } else if (rc == -2) {
            snprintf(response, response_size, "ERROR ROLE");
        } else if (rc == -3) {
            snprintf(response, response_size, "ERROR NOT_FOUND");
        } else if (rc == -5) {
            snprintf(response, response_size, "ERROR MITIGATE_TIMEOUT ATTACKER_POINTS +10");
        } else {
            snprintf(response, response_size, "ERROR MITIGATE");
        }
        return 0;
    }

    if (strcmp(cmd, "STATUS") == 0) {
        int rc = game_status(client->player_id, response, response_size);
        if (rc != 0) {
            snprintf(response, response_size, "ERROR PLAYER_NOT_FOUND");
        }
        return 0;
    }

    snprintf(response, response_size, "ERROR UNKNOWN_COMMAND");
    return 0;
}
