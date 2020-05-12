_: clean matrix test

matrix:
	cc -Wall -g -c matrix.c
	cc -g -o matrix matrix.o -ljack -llo

test:
	cc -Wall -g -c test.c
	cc -g -o test test.o -ljack -llo

clean:
	rm -f *.o
	rm -f matrix test
