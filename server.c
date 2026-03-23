#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define MAX_PLAYERS 100
#define MAX_RESOURCES 2

typedef struct {

    int socket;
    int x;
    int y;
    char role[10];

} Player;

typedef struct {

    char name[20];
    int x;
    int y;

} Resource;

Player players[MAX_PLAYERS];
int player_count = 0;

Resource resources[MAX_RESOURCES] = {
    {"server1",30,40},
    {"server2",70,80}
};

Player* get_player(int client_socket){

    for(int i=0;i<player_count;i++){

        if(players[i].socket == client_socket){
            return &players[i];
        }
    }

    return NULL;
}

void add_player(int client_socket){

    if(player_count >= MAX_PLAYERS)
        return;

    players[player_count].socket = client_socket;
    players[player_count].x = 0;
    players[player_count].y = 0;

    strcpy(players[player_count].role,"attacker");

    player_count++;

    printf("Jugador agregado. Total: %d\n",player_count);
}

void move_player(int client_socket, int x, int y){

    for(int i=0;i<player_count;i++){

        if(players[i].socket == client_socket){

            players[i].x = x;
            players[i].y = y;

            printf("Jugador %d se movió a (%d,%d)\n",i,x,y);

            break;
        }
    }
}

void scan_for_resources(int client_socket){

    Player *p = get_player(client_socket);

    if(p == NULL)
        return;

    for(int i=0;i<MAX_RESOURCES;i++){

        int dx = abs(p->x - resources[i].x);
        int dy = abs(p->y - resources[i].y);

        if(dx <= 5 && dy <= 5){

            char response[100];

            sprintf(response,"RESOURCE_FOUND %s\n",resources[i].name);

            send(client_socket,response,strlen(response),0);

            return;
        }
    }

    char response[]="NOTHING_FOUND\n";

    send(client_socket,response,strlen(response),0);
}

void process_command(int client_socket, char *buffer){

    char *command = strtok(buffer," ");

    if(command == NULL)
        return;

    if(strcmp(command,"MOVE") == 0){

        char *x = strtok(NULL," ");
        char *y = strtok(NULL," ");

        if(x && y){

            move_player(client_socket,atoi(x),atoi(y));

            char response[]="OK MOVE\n";
            send(client_socket,response,strlen(response),0);
        }
        else{

            char response[]="ERROR PARAMS\n";
            send(client_socket,response,strlen(response),0);
        }
    }

    else if(strcmp(command,"SCAN") == 0){

    scan_for_resources(client_socket);
    }

    else{

        char response[]="ERROR UNKNOWN_COMMAND\n";
        send(client_socket,response,strlen(response),0);
    }
}

void *handle_client(void *socket_desc){

    int client_socket = *(int*)socket_desc;
    add_player(client_socket);
    char buffer[BUFFER_SIZE];

    while(1){

        memset(buffer,0,BUFFER_SIZE);

        int read_size = recv(client_socket,buffer,BUFFER_SIZE,0);
        
        if(read_size <= 0){
            break;
        }

        buffer[strcspn(buffer, "\r\n")] = 0;

        printf("Mensaje recibido: %s\n",buffer);
        process_command(client_socket,buffer);

    }

    close(client_socket);

    return NULL;
}

int main(int argc,char *argv[]){

    if(argc < 2){
        printf("Uso: %s <puerto>\n",argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    int server_socket = socket(AF_INET,SOCK_STREAM,0);

    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    bind(server_socket,(struct sockaddr*)&server_addr,sizeof(server_addr));

    listen(server_socket,10);

    printf("Servidor escuchando en puerto %d\n",port);

    while(1){

        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);

        int client_socket = accept(server_socket,
        (struct sockaddr*)&client_addr,
        (socklen_t*)&client_len);

        pthread_t thread_id;

        pthread_create(&thread_id,NULL,handle_client,(void*)&client_socket);
    }

    close(server_socket);

    return 0;
}


