_: clean matrix test

matrix:
	cc -std=c11 -Wall -g -c matrix.c
	cc -g -o matrix matrix.o -ljack -llo -lpthread -lm

test:
	cc -std=c11 -Wall -g -c test.c
	cc -g -o test test.o -ljack -llo -lm

clean:
	rm -f *.o
	rm -f matrix test
