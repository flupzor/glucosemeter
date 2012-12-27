CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= `pkg-config --cflags gtk+-2.0`
LDADD+= `pkg-config --libs gtk+-2.0`
LDADD+= -lsqlite3
LDADD+= -lbsd

.c.o:
	$(CC) -c $(CFLAGS) $<

glucosemeter: glucosemeter.o abfr.o
	$(CC) -o glucosemeter glucosemeter.o abfr.o $(LDADD)

clean:
	rm *.o glucosemeter
