PROG=	glucosemeter
SRCS=	glucosemeter.c abfr.c devicemgmt.c parse.y

MAN=	

CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= `pkg-config --cflags gtk+-2.0`
LDADD+= `pkg-config --libs gtk+-2.0`
LDADD+= -lsqlite3
YFLAGS=

.include <bsd.prog.mk>
