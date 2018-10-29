#
DIR := ../raspberry_pi

CC :=gcc
# インクルードファイル等
CFLAGS := -g -DPACKET_DUMP -I $(DIR)

LDFLAGS :=

LIB := -lpthread

TARGETS := nwebsocket #nlogger

all:$(TARGETS)

nwebsocket: $(DIR)/main.c $(DIR)/websocket.c
	$(CC) $(CFLAGS) $(DIR)/main.c $(DIR)/websocket.c $(LIB) -o nwebsocket

clean:
	rm -rf $(TARGETS) *.o


