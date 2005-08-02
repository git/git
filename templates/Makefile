# make

INSTALL=install
prefix=$(HOME)
etcdir=$(prefix)/etc
etcgitdir=$(etcdir)/git-core
templatedir=$(etcgitdir)/templates
# dest=

all:
clean:

install:
	$(INSTALL) -d -m755 $(dest)$(templatedir)/hooks/
	$(foreach s,$(wildcard hooks--*),\
		$(INSTALL) -m644 $s \
		$(dest)$(templatedir)/hooks/$(patsubst hooks--%,%,$s);)
	$(INSTALL) -d -m755 $(dest)$(templatedir)/info
	$(INSTALL) -d -m755 $(dest)$(templatedir)/branches
