#ifndef LOGGER_H
#define LOGGER_H

void logger_init(const char *path);
void logger_close(void);
void logger_server(const char *message);
void logger_client(const char *ip, int port, const char *message);
void logger_metrics(const char *message);

#endif
