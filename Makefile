OBJ=ddhcp.o netsock.o packet.o dhcp.o dhcp_packet.o dhcp_options.o tools.o block.o

CC=gcc
CFLAGS+= \
    -Wall \
    -Wextra \
    -pedantic \
    -Wcast-align \
    -Werror \
    -flto \
    -fno-strict-aliasing \
    -std=gnu11 \
    -MD -MP
LFLAGS+= \
    -flto \
    -lm

ifeq ($(DEBUG),1)
CFLAGS+= \
    -g \
    -fsanitize=address
LFLAGS+= \
    -g \
    -fsanitize=address
endif

all: ddhcp

ddhcp: ${OBJ}
	${CC} ${OBJ} ${CFLAGS} -o ddhcp ${LFLAGS}

clean:
	-rm -f ddhcp ${OBJ} *.d *.orig

style:
	astyle --mode=c --options=none -s2 -f -j *.c *.h
