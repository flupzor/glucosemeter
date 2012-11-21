CFLAGS = -g 
ALL_LIBS = -lsqlite3 `pkg-config --libs gtk+-2.0` -lbsd
ALL_CFLAGS = `pkg-config --cflags gtk+-2.0` -I. $(CFLAGS)

.c.o:
	$(CC) -c  $(ALL_CFLAGS) $<

glucosemeter: glucosemeter.o abbott.o
	$(CC) -o glucosemeter glucosemeter.o abbott.o $(ALL_LIBS)

clean:
	rm *.o glucosemeter
