CC=gcc
CFLAGS=-Wall -Wextra -std=c11 -pthread

SRC=main.c \
    network/network.c \
    protocol/protocol.c \
    game/game.c \
    utils/logger.c

TARGET=cyber_server

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
