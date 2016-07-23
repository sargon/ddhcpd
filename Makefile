OBJ=ddhcp.o netsock.o packet.o dhcp.o dhcp_packet.o dhcp_options.o tools.o block.o

CC=gcc
CFLAGS+= \
    -g \
    -Wall \
    -Wextra \
    -pedantic \
    -Wcast-align \
    -Werror \
    -flto \
    -fno-strict-aliasing \
    -fsanitize=address \
    -std=gnu11 \
    -MD -MP
LFLAGS+= \
    -g \
    -flto \
    -fsanitize=address \
    -lm

all: ${OBJ}
	gcc ${OBJ} ${CFLAGS} -o ddhcp ${LFLAGS}

dhcp: dhcp.o netsock.o
	gcc ${CFLAGS} netsock.o dhcp.o -o dhcp ${LFLAGS}

clean:
	rm -f ddhcp ${OBJ} *.d || true

style:
	astyle --mode=c --options=none -s2 -f -j *.c *.h
