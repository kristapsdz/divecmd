.PHONY: clean

VERSION		 = 0.0.1
LDFLAGS		+= -L/usr/local/lib -ldivecomputer
CFLAGS		+= -g -I/usr/local/include -W -Wall -DVERSION="\"$(VERSION)\""
OBJS		 = common.o \
		   main.o \
		   download.o \
		   list.o \
		   xml.o

all: divecmd divecmd2term

divecmd: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) 

divecmd2term: divecmd2term.o
	$(CC) -o $@ divecmd2term.o -lexpat

$(OBJS): extern.h

clean:
	rm -f $(OBJS) divecmd
