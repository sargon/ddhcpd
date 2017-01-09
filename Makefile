OBJ=ddhcp.o netsock.o packet.o dhcp.o dhcp_packet.o dhcp_options.o tools.o block.o
OBJCTL=ddhcpctl.o netsock.o packet.o dhcp.o dhcp_packet.o dhcp_options.o tools.o block.o

CC=gcc
CFLAGS+= \
    -Wall \
    -Wextra \
    -pedantic \
    -Werror \
    -flto \
    -fno-strict-aliasing \
    -std=gnu11 \
		-D_GNU_SOURCE \
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

prefix?=/usr
INSTALL = install
INSTALL_FILE    = $(INSTALL) -D -p    -m  644
INSTALL_PROGRAM = $(INSTALL) -D -p    -m  755
INSTALL_SCRIPT  = $(INSTALL) -D -p    -m  755
INSTALL_DIR     = $(INSTALL) -D -p -d -m  755

all: ddhcpd ddhcpdctl

ddhcpd: ${OBJ}
	${CC} ${OBJ} ${CFLAGS} -o ddhcpd ${LFLAGS}

ddhcpdctl: ${OBJCTL}
	${CC} ${OBJCTL} ${CFLAGS} -o ddhcpdctl ${LFLAGS}

clean:
	-rm -f ddhcpd ddhcpdctl ${OBJ} *.d *.orig

style:
	astyle --mode=c --options=none -s2 -f -j -k1 -W3 -p -U -H *.c *.h

install:
	$(INSTALL_PROGRAM) ddhcpd $(DESTDIR)$(prefix)/sbin/ddhcpd
