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
BINOBJS		 = divecmd2grap.o \
		   divecmd2json.o \
		   divecmd2term.o \
		   parser.o
BINDIR 		 = $(PREFIX)/bin
MANDIR 		 = $(PREFIX)/man
BINS		 = divecmd \
		   divecmd2grap \
		   divecmd2json \
		   divecmd2term
MAN1S		 = divecmd.1 \
		   divecmd2grap.1 \
		   divecmd2json.1 \
		   divecmd2term.1

all: $(BINS)

divecmd: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) 

divecmd2term: divecmd2term.o parser.o
	$(CC) -o $@ divecmd2term.o parser.o -lexpat -lm

divecmd2grap: divecmd2grap.o parser.o
	$(CC) -o $@ divecmd2grap.o parser.o -lexpat

divecmd2json: divecmd2json.o parser.o
	$(CC) -o $@ divecmd2json.o parser.o -lexpat

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -m 0755 $(BINS) $(DESTDIR)$(BINDIR)
	install -m 0444 $(MAN1S) $(DESTDIR)$(MANDIR)/man1

$(OBJS): extern.h

$(BINOBJS): parser.h

clean:
	rm -f $(OBJS) $(BINS) $(BINOBJS)
