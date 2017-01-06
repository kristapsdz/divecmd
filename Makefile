.PHONY: clean

PREFIX 		 = /usr/local
VERSION		 = 0.0.1
LDFLAGS		+= -L/usr/local/lib -ldivecomputer
CFLAGS		+= -g -I/usr/local/include -W -Wall -DVERSION="\"$(VERSION)\""
OBJS		 = common.o \
		   main.o \
		   download.o \
		   list.o \
		   xml.o
BINDIR 		 = $(PREFIX)/bin
MANDIR 		 = $(PREFIX)/man

all: divecmd divecmd2term

divecmd: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) 

divecmd2term: divecmd2term.o parser.o
	$(CC) -o $@ divecmd2term.o parser.o -lexpat -lm

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -m 0755 divecmd divecmd2term $(DESTDIR)$(BINDIR)
	install -m 0444 divecmd.1 divecmd2term.1 $(DESTDIR)$(MANDIR)/man1

$(OBJS): extern.h

clean:
	rm -f $(OBJS) divecmd 
	rm -f divecmd2term.o parser.o divecmd2term
