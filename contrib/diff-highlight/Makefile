all: diff-highlight

PERL_PATH = /usr/bin/perl
-include ../../config.mak

PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))

diff-highlight: shebang.perl DiffHighlight.pm diff-highlight.perl
	cat $^ >$@+
	chmod +x $@+
	mv $@+ $@

shebang.perl: FORCE
	@echo '#!$(PERL_PATH_SQ)' >$@+
	@cmp $@+ $@ >/dev/null 2>/dev/null || mv $@+ $@

test: all
	$(MAKE) -C t

clean:
	$(RM) diff-highlight

.PHONY: FORCE
