PYTHONVER=python2.5
CFLAGS=-g -I/usr/include/${PYTHONVER} -fPIC -Wall

spuhelper.so : spuhelper.o
	$(CC) $^ -L${PYTHONVER}/config -l${PYTHONVER} -shared -o $@

sortmail.o : sortmail.c

clean :
	rm -f spuhelper.so spuhelper.o

.PHONY : clean
