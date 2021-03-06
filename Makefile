PREFIX ?= /usr/local
INSTALLDIR := $(DESTDIR)$(PREFIX)

CC = gcc

PKGS = keybinder gio-unix-2.0
INCS = $(shell pkg-config --cflags $(PKGS))
LIBS = $(shell pkg-config --libs $(PKGS))

CFLAGS	:= -Wall -Wextra -Wno-unused-parameter -pthread -std=c99 $(INCS) $(CFLAGS)
LDFLAGS	:= -pthread -lm $(LIBS) $(LDFLAGS)

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

ifeq ($(DEBUG),1)
    OFLAGS = -DDEBUG -g
else
    OFLAGS = -DNDEBUG -O2 
endif

all: fehlstart

fehlstart: $(OBJS)
	$(CC) -o $@ $(OFLAGS) $(OBJS) $(LDFLAGS)

.c.o:
	$(CC) -c $(OFLAGS) $(CFLAGS) $(CPPFLAGS) $< -o $@

install:
	install -Dsm 755 fehlstart $(INSTALLDIR)/bin/fehlstart

clean:
	rm -rf fehlstart $(OBJS)
