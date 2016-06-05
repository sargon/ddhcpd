OBJ=ddhcp.o netsock.o packet.o dhcp.o dhcp_packet.o dhcp_options.o tools.o block.o 

CC=gcc
CFLAGS+=-Wall -fno-strict-aliasing -MD -MP -g

all: ${OBJ}
	gcc ${OBJ} -o ddhcp -lm

dhcp: dhcp.o netsock.o
	gcc netsock.o dhcp.o -o dhcp

clean:
	rm ddhcp ${OBJ} *.d || true
