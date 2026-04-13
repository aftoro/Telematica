#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

#include "../network/client.h"

int protocol_process_command(ClientConnection *client, const char *line, char *response, size_t response_size);

#endif
