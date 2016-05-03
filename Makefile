CC=gcc
CFLAGS=-g -Wall -std=c11 -fPIC
OBJS=parser.o pbquery.o init.o

libpbquery.so: $(OBJS)
	ld -shared $(OBJS) -lprotobuf-c -o $@

install: libpbquery.so
	cp libpbquery.so /usr/local/lib
	ldconfig
