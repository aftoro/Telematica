CC=gcc
CFLAGS=-Wall -Wextra -std=c11

TARGET=http_server
SRC=http_server.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
