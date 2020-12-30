all: diff-highlight

PERL_PATH = /usr/bin/perl
-include ../../config.mak

PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))

diff-highlight: shebang.perl DiffHighlight.pm diff-highlight.perl
	cat $^ >$@+
	chmod +x $@+
	mv $@+ $@

linked-in-destdir: diff-highlight
	test -n "$(DESTDIR)" && \
		test -w $(DESTDIR) && \
		ln -s $(abspath $<) $(DESTDIR)

shebang.perl: FORCE
	@echo '#!$(PERL_PATH_SQ)' >$@+
	@cmp $@+ $@ >/dev/null 2>/dev/null || mv $@+ $@

test: all
	$(MAKE) -C t

unlink-from-destdir:
	test -z "$(DESTDIR)" || \
		test ! -L $(DESTDIR)/diff-highlight || \
		$(RM) $(DESTDIR)/diff-highlight

clean: unlink-from-destdir
	$(RM) diff-highlight

.PHONY: FORCE
.PHONY: linked-in-destdir unlink-from-destdir
