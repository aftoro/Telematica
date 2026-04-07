# Protocolo de Aplicacion - Simulador de Ciberseguridad

## 1. Descripcion General

El servidor TCP usa un protocolo de texto orientado a linea.
Cada mensaje termina en salto de linea (`\n`).

Formato base:

```
COMANDO ARG1 ARG2 ...\n
```

Respuesta base:

```
OK <accion>
ERROR <mensaje>
DATA <tipo> <valores>
ALERT <resource_id>
```

## 2. Comandos del Cliente

### ROLE rol nombre
Configura identidad del jugador al conectarse.

Ejemplo:

```
ROLE ATACANTE alice
-> OK ROLE
```

### MOVE x y
Mueve al jugador en el mapa 2D (0..100, 0..100).

```
MOVE 10 20
-> OK MOVE
```

### SCAN
Descubre recursos cercanos (vision limitada para ATACANTE).

```
SCAN
-> DATA SCAN 1:server1:30:40:0
```

### ATTACK id
Solo ATACANTE. Ataca recurso cercano.

```
ATTACK 1
-> OK ATTACK 1
```

### MITIGATE id
Solo DEFENSOR. Mitiga recursos atacados.

```
MITIGATE 1
-> OK MITIGATE 1 RELOCATED 12 34
```

Si el tiempo de mitigacion expira:

```
MITIGATE 1
-> ERROR MITIGATE_TIMEOUT ATTACKER_POINTS +10
```

### STATUS
Devuelve snapshot del juego visible para el jugador.

```
STATUS
-> DATA STATUS PLAYERS 0:alice:ATACANTE:10:20:5 RESOURCES 1:server1:30:40:0
```

Formato de players:

```
id:nombre:rol:x:y:puntos
```

## 3. Alertas del Servidor

Cuando un recurso es atacado, el servidor envia:

```
ALERT <resource_id>
```

Esto se envia de forma asincrona a los DEFENSORES conectados.

## 4. Estados de Recursos

Campo `state`:

- `0`: NORMAL
- `1`: ATTACKED
- `2`: COMPROMISED (no mitigado a tiempo)

## 5. Flujo de Comunicacion

1. Cliente TCP conecta al servidor de juego.
2. Envia `ROLE` para definir ATACANTE o DEFENSOR.
3. Envia comandos (`MOVE`, `SCAN`, `ATTACK`, `MITIGATE`, `STATUS`).
4. El servidor valida en `protocol/`, ejecuta en `game/` y responde.
5. Cuando aplica, se emiten `ALERT` a defensores.

## 6. Integracion Web

El frontend no abre socket TCP directo.
Usa proxy HTTP (`/api/command`) que reenvia comandos al socket TCP asociado a la sesion.
