#
# Makefile for the git_remote_helpers python support modules
#
pysetupfile:=setup.py

# Shell quote (do not use $(call) to accommodate ancient setups);
DESTDIR_SQ = $(subst ','\'',$(DESTDIR))

ifndef PYTHON_PATH
	ifeq ($(uname_S),FreeBSD)
		PYTHON_PATH = /usr/local/bin/python
	else
		PYTHON_PATH = /usr/bin/python
	endif
endif
ifndef prefix
	prefix = $(HOME)
endif
ifndef V
	QUIET = @
	QUIETSETUP = --quiet
endif

PYLIBDIR=$(shell $(PYTHON_PATH) -c \
	 "import sys; \
	 print 'lib/python%i.%i/site-packages' % sys.version_info[:2]")

all: $(pysetupfile)
	$(QUIET)$(PYTHON_PATH) $(pysetupfile) $(QUIETSETUP) build

install: $(pysetupfile)
	$(PYTHON_PATH) $(pysetupfile) install --prefix $(DESTDIR_SQ)$(prefix)

instlibdir: $(pysetupfile)
	@echo "$(DESTDIR_SQ)$(prefix)/$(PYLIBDIR)"

clean:
	$(QUIET)$(PYTHON_PATH) $(pysetupfile) $(QUIETSETUP) clean -a
	$(RM) *.pyo *.pyc
