PREFIX ?= /usr/local
INSTALLDIR := $(DESTDIR)$(PREFIX)

CC := gcc

PKGS := gtk+-2.0 gio-unix-2.0
INCS := $(shell pkg-config --cflags $(PKGS))
LIBS := $(shell pkg-config --libs $(PKGS))

CFLAGS	:= -g -Wall -Wextra -std=gnu99 $(INCS) $(CFLAGS)
LDFLAGS	:= $(LIBS) $(LDFLAGS)

SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)
all: fehlstart

fehlstart: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

.c.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

clean:
	rm -rf fehlstart $(OBJS)
