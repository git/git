# make and install sample templates

ifndef V
	QUIET = @
endif

INSTALL ?= install
TAR ?= tar
prefix ?= $(HOME)
template_dir ?= $(prefix)/share/git-core/templates/
# DESTDIR=

# Shell quote (do not use $(call) to accommodate ancient setups);
DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
template_dir_SQ = $(subst ','\'',$(template_dir))

all: boilerplates.made custom

# Put templates that can be copied straight from the source
# in a file direc--tory--file in the source.  They will be
# just copied to the destination.

bpsrc = $(filter-out %~,$(wildcard *--*))
boilerplates.made : $(bpsrc)
	$(QUIET)ls *--* 2>/dev/null | \
	while read boilerplate; \
	do \
		case "$$boilerplate" in *~) continue ;; esac && \
		dst=`echo "$$boilerplate" | sed -e 's|^this|.|;s|--|/|g'` && \
		dir=`expr "$$dst" : '\(.*\)/'` && \
		mkdir -p blt/$$dir && \
		case "$$boilerplate" in \
		*--) ;; \
		*) cp $$boilerplate blt/$$dst ;; \
		esac || exit; \
	done && \
	date >$@

# If you need build-tailored templates, build them into blt/
# directory yourself here.
custom:
	$(QUIET): no custom templates yet

clean:
	rm -rf blt boilerplates.made

install: all
	$(INSTALL) -d -m755 '$(DESTDIR_SQ)$(template_dir_SQ)'
	(cd blt && $(TAR) cf - .) | \
	(cd '$(DESTDIR_SQ)$(template_dir_SQ)' && $(TAR) xf -)
