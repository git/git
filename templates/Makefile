# make and install sample templates

INSTALL=install
prefix=$(HOME)
template_dir=$(prefix)/share/git-core/templates/
# DESTDIR=

all: boilerplates custom
	find blt

# Put templates that can be copied straight from the source
# in a file direc--tory--file in the source.  They will be
# just copied to the destination.
boilerplates:
	ls *--* 2>/dev/null | \
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
	done || exit

# If you need build-tailored templates, build them into blt/
# directory yourself here.
custom:
	: no custom templates yet

clean:
	rm -rf blt

install: all
	$(INSTALL) -d -m755 $(DESTDIR)$(template_dir)
	tar Ccf blt - . | tar Cxf $(DESTDIR)$(template_dir) -
