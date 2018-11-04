#
DIR := ../raspberry_pi

CC :=gcc

CFLAGS := -g -DPACKET_DUMP -I $(DIR)

LDFLAGS :=

LIB := -lpthread

TARGETS := nwebsocket logger monitor

all:$(TARGETS)

nwebsocket: $(DIR)/main.c $(DIR)/websocket.c
	$(CC) $(CFLAGS) $(DIR)/main.c $(DIR)/websocket.c $(LIB) -o nwebsocket

logger: $(DIR)/logger.c $(DIR)/websocket.c
	$(CC) $(CFLAGS) $(DIR)/logger.c $(DIR)/websocket.c $(LIB) -o logger

monitor: $(DIR)/testmain.c $(DIR)/monitor.c $(DIR)/websocket.c
	$(CC) $(CFLAGS) $(DIR)/testmain.c $(DIR)/monitor.c $(DIR)/websocket.c $(LIB) -o monitor

clean:
	rm -rf $(TARGETS) *.o


