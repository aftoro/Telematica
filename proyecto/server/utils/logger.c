#include "logger.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void logger_write_locked(const char *prefix, const char *message) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    fprintf(stdout, "%s %s %s\n", ts, prefix, message);
    fflush(stdout);

    if (g_log_file != NULL) {
        fprintf(g_log_file, "%s %s %s\n", ts, prefix, message);
        fflush(g_log_file);
    }
}

void logger_init(const char *path) {
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file == NULL) {
        g_log_file = fopen(path, "a");
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void logger_close(void) {
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void logger_server(const char *message) {
    pthread_mutex_lock(&g_log_mutex);
    logger_write_locked("[SERVER] ->", message);
    pthread_mutex_unlock(&g_log_mutex);
}

void logger_client(const char *ip, int port, const char *message) {
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "[%s:%d] ->", ip, port);

    pthread_mutex_lock(&g_log_mutex);
    logger_write_locked(prefix, message);
    pthread_mutex_unlock(&g_log_mutex);
}

void logger_metrics(const char *message) {
    pthread_mutex_lock(&g_log_mutex);
    logger_write_locked("[METRICS]", message);
    pthread_mutex_unlock(&g_log_mutex);
}
