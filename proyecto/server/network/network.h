#ifndef NETWORK_H
#define NETWORK_H

int network_start_server(int port);
int network_send_to_socket(int socket, const char *line);

#endif
