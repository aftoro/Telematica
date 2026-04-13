#include "config.h"
#include "game/game.h"
#include "network/network.h"
#include "utils/logger.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0) {
        fprintf(stderr, "Puerto invalido\n");
        return 1;
    }

    logger_init(LOG_FILE);
    game_init();

    int rc = network_start_server(port);

    logger_close();
    return rc == 0 ? 0 : 1;
}
