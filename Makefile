OBJ=ddhcp.o netsock.o

CC=gcc
CFLAGS+=-pedantic -Wall -W -std=gnu99 -fno-strict-aliasing -MD -MP

all: ${OBJ}
	gcc --std=c11 ${OBJ} -o ddhcp

clean:
	rm ${OBJ}
