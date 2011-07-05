PREFIX ?= /usr/local
INSTALLDIR := $(DESTDIR)$(PREFIX)

CC := gcc

PKGS := keybinder gio-unix-2.0
INCS := $(shell pkg-config --cflags $(PKGS))
LIBS := $(shell pkg-config --libs $(PKGS))

CFLAGS	:= -Wall -Wextra -std=gnu99 $(INCS) $(CFLAGS)
LDFLAGS	:= $(LIBS) $(LDFLAGS)

SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)

all: CC += -DNDEBUG -O2
all: fehlstart

debug: CC += -DDEBUG -g
debug: fehlstart

fehlstart: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

.c.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

install:
	install -d $(INSTALLDIR)/bin
	install -m 755 fehlstart $(INSTALLDIR)/bin/fehlstart

clean:
	rm -rf fehlstart $(OBJS)
