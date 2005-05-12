DOC_SRC=$(wildcard git*.txt)
DOC_HTML=$(patsubst %.txt,%.html,$(DOC_SRC))
DOC_MAN=$(patsubst %.txt,%.1,$(DOC_SRC))

all: $(DOC_HTML) $(DOC_MAN)

html: $(DOC_HTML)

man: $(DOC_MAN)

# 'include' dependencies
git-diff-%.txt: diff-format.txt
	touch $@

clean:
	rm -f *.xml *.html *.1

%.html : %.txt
	asciidoc -b css-embedded -d manpage $<

%.1 : %.xml
	xmlto man $<

%.xml : %.txt
	asciidoc -b docbook -d manpage $<
