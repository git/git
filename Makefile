all:

clean:
	rm -f Subpro.html


all: Subpro.html

%.html: %.txt
	asciidoc -bxhtml11 $*.txt

