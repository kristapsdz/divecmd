.PHONY: clean

LDFLAGS		+= -L/usr/local/lib -ldivecomputer
CFLAGS		+= -g -I/usr/local/include -W -Wall
OBJS		 = common.o \
		   main.o \
		   download.o \
		   list.o \
		   output.o

divecmd: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) 

$(OBJS): extern.h

clean:
	rm -f $(OBJS) divecmd
