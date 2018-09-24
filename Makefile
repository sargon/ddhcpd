OBJ=main.o ddhcp.o netsock.o packet.o dhcp.o dhcp_packet.o dhcp_options.o tools.o block.o control.o hook.o logger.o
OBJCTL=ddhcpctl.o netsock.o packet.o dhcp.o dhcp_packet.o dhcp_options.o tools.o block.o hook.o logger.o
HDRS=$(wildcard *.h)

REVISION=$(shell git rev-list --first-parent HEAD --max-count=1)

CC=gcc
CFLAGS+= \
    -Wall \
    -Wextra \
    -pedantic \
    -Werror \
    -flto \
    -fno-strict-aliasing \
    -std=c11 \
    -D_GNU_SOURCE \
    -MD -MP \
    -masm=intel \
    -Wduplicated-cond \
    -Wduplicated-branches \
    -Wlogical-op \
    -Wrestrict \
    -Wnull-dereference \
    -Wdouble-promotion \
    -Wshadow \
    -Wformat=2 \
    -Wfloat-equal \
    -Wundef \
    -Wpointer-arith \
    -Wcast-align \
    -Wstrict-overflow=5 \
    -Wwrite-strings \
    -Wswitch-default \
    -Wswitch-enum \
    -Wconversion \
    -Wunreachable-code \
    -Winit-self

CXXFLAGS+= \
    ${CFLAGS} \
    -std=c++11 \
    -Wuseless-cast \
    -Weffc++

LFLAGS+= \
    -flto \
    -lm

ifeq ($(DEBUG),1)
CFLAGS+= \
    -Og -g \
    -fsanitize=address,signed-integer-overflow,undefined \

LFLAGS+= \
    -Og -g \
    -fsanitize=address,signed-integer-overflow,undefined
else
	CFLAGS+= \
		-DNDEBUG
endif

prefix?=/usr
INSTALL = install
INSTALL_FILE    = $(INSTALL) -D -p    -m  644
INSTALL_PROGRAM = $(INSTALL) -D -p    -m  755
INSTALL_SCRIPT  = $(INSTALL) -D -p    -m  755
INSTALL_DIR     = $(INSTALL) -D -p -d -m  755

all: ddhcpd ddhcpdctl

.PHONY: version.h
version.h:
	echo '#define REVISION "$(REVISION)"' > version.h

ddhcpd: version.h ${OBJ} ${HDRS}
	${CC} ${OBJ} ${CFLAGS} -o ddhcpd ${LFLAGS}

ddhcpdctl: version.h ${OBJCTL} ${HDRS}
	${CC} ${OBJCTL} ${CFLAGS} -o ddhcpdctl ${LFLAGS}

clean:
	-rm -f ddhcpd ddhcpdctl
	-rm -f ${OBJ}
	-rm -f ${OBJCTL}
	-rm -f *.d
	-rm -f *.orig

style:
	astyle --mode=c --options=none -s2 -f -j -k1 -W3 -p -U -H *.c *.h

install:
	$(INSTALL_PROGRAM) ddhcpd $(DESTDIR)$(prefix)/sbin/ddhcpd

-include *.d
