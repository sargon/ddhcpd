OBJ=ddhcp.o netsock.o packet.o dhcp.o

CC=gcc
CFLAGS+=-Wall -W -std=gnu99 -fno-strict-aliasing -MD -MP -g 

all: ${OBJ}
	gcc --std=c11 ${OBJ} -o ddhcp -lm

dhcp: dhcp.o netsock.o
	gcc --std=c11 netsock.o dhcp.o -o dhcp

clean:
	rm ${OBJ}
