default:
	@echo "git-subtree doesn't need to be built."
	@echo "Just copy it somewhere on your PATH, like /usr/local/bin."
	@echo
	@echo "Try: make doc"
	@false

doc: git-subtree.1

%.1: %.xml
	xmlto -m manpage-normal.xsl  man $^

%.xml: %.txt
	asciidoc -b docbook -d manpage -f asciidoc.conf \
		-agit_version=1.6.3 $^

clean:
	rm -f *~ *.xml *.html *.1
	rm -rf subproj mainline
