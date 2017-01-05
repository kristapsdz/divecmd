.PHONY: clean

VERSION		 = 0.0.1
LDFLAGS		+= -L/usr/local/lib -ldivecomputer
CFLAGS		+= -g -I/usr/local/include -W -Wall -DVERSION="\"$(VERSION)\""
OBJS		 = common.o \
		   main.o \
		   download.o \
		   list.o \
		   xml.o

divecmd: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) 

$(OBJS): extern.h

clean:
	rm -f $(OBJS) divecmd
