DOC_SRC=$(wildcard git*.txt)
DOC_HTML=$(patsubst %.txt,%.html,$(DOC_SRC))
DOC_MAN=$(patsubst %.txt,%.1,$(wildcard git-*.txt)) git.7

all: $(DOC_HTML) $(DOC_MAN)

html: $(DOC_HTML)

man: $(DOC_MAN)

# 'include' dependencies
git-diff-%.txt: diff-format.txt
	touch $@

clean:
	rm -f *.xml *.html *.1 *.7

%.html : %.txt
	asciidoc -b css-embedded -d manpage $<

%.1 %.7 : %.xml
	xmlto man $<
	# FIXME: this next line works around an output filename bug in asciidoc 6.0.3
	[ "$@" = "git.7" ] || mv git.1 $@

%.xml : %.txt
	asciidoc -b docbook -d manpage $<
