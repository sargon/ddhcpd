OBJ=ddhcp.o netsock.o packet.o

CC=gcc
CFLAGS+=-pedantic -Wall -W -std=gnu99 -fno-strict-aliasing -MD -MP -g

all: ${OBJ}
	gcc --std=c11 ${OBJ} -o ddhcp

dhcp: dhcp.o netsock.o
	gcc --std=c11 netsock.o dhcp.o -o dhcp

clean:
	rm ${OBJ}
