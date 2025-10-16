all: inicializador emisor

inicializador: inicializador.c
	gcc inicializador.c -o inicializador -Wall

emisor: emisor.c
	gcc emisor.c -o emisor -Wall

clean:
	rm -f inicializador emisor
