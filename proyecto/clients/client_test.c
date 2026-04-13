#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h> 

static int recv_line(int sock, char *out, size_t size) {
    size_t pos = 0;
    while (pos + 1 < size) {
        char c = '\0';
        ssize_t n = recv(sock, &c, 1, 0);
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
    return (int)pos;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ip> <puerto>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

 

struct addrinfo hints, *res;
memset(&hints, 0, sizeof(hints));
hints.ai_family = AF_INET;
hints.ai_socktype = SOCK_STREAM;

char port_str[10];
sprintf(port_str, "%d", port);

if (getaddrinfo(ip, port_str, &hints, &res) != 0) {
    perror("getaddrinfo");
    close(sock);
    return 1;
}

if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
    perror("connect");
    freeaddrinfo(res);
    close(sock);
    return 1;
}

freeaddrinfo(res);

    char line[512];
    if (recv_line(sock, line, sizeof(line)) > 0) {
        printf("< %s\n", line);
    }

    printf("Escribe comandos (ROLE, MOVE, SCAN, ATTACK, MITIGATE, STATUS). Ctrl+D para salir.\n");

    char input[512];
    while (fgets(input, sizeof(input), stdin) != NULL) {
        size_t len = strlen(input);
        if (len == 0 || input[len - 1] != '\n') {
            strcat(input, "\n");
        }

        if (send(sock, input, strlen(input), 0) < 0) {
            perror("send");
            break;
        }

        if (recv_line(sock, line, sizeof(line)) <= 0) {
            printf("Conexion cerrada\n");
            break;
        }

        printf("< %s\n", line);
    }

    close(sock);
    return 0;
}
