# Simulador Multijugador de Ciberseguridad

Proyecto modular con servidor TCP en C, proxy HTTP en C y frontend web con Canvas.

## Estructura

- `server/`: servidor de juego TCP (network/protocol/game/utils)
- `http_server/`: servidor HTTP con lobby y proxy de comandos
- `frontend/`: interfaz web (login, lobby, juego)
- `clients/`: cliente de prueba en C
- `docs/`: documentacion del protocolo

## Compilacion

### 1) Servidor de juego TCP

```bash
cd server
make
```

### 2) Servidor HTTP

```bash
cd ../http_server
make
```

### 3) Cliente de prueba (opcional)

```bash
cd ../clients
gcc -Wall -Wextra -std=c11 -o client_test client_test.c
```

## Ejecucion

En terminal 1 (servidor TCP):

```bash
cd server
./cyber_server 9090
```

En terminal 2 (HTTP + frontend):

```bash
cd ../http_server
./http_server 8080
```

Abrir navegador en:

- `http://localhost:8080`

## Protocolo

Revisar `docs/protocolo.md` para comandos, respuestas y flujo.
