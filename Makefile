PREFIX ?= /usr/local
INSTALLDIR := $(DESTDIR)$(PREFIX)

CC := gcc

PKGS := gtk+-2.0 gio-unix-2.0
INCS := $(shell pkg-config --cflags $(PKGS))
LIBS := $(shell pkg-config --libs $(PKGS))

CFLAGS	:= -Wall -Wextra -std=gnu99 -I. $(INCS) $(CFLAGS)
LDFLAGS	:= $(LIBS) $(LDFLAGS)

SRCS := fehlstart.c
OBJS := $(SRCS:.c=.o)
all: fehlstart

fehlstart: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

.c.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

clean:
	rm -rf fehlstart $(OBJS)
