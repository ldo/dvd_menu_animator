# This Makefile is now used for testing only.

PYTHONVER=python3.4m
CFLAGS=-g -I/usr/include/${PYTHONVER} -fPIC -Wall

spuhelper.so : spuhelper.o
	$(CC) $^ -L${PYTHONVER}/config -l${PYTHONVER} -lpng -shared -o $@

spuhelper.o : spuhelper.c

clean :
	rm -f spuhelper.so spuhelper.o build/

.PHONY : clean
