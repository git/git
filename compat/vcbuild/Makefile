## Makefile to install nuget package dependencies.
##################################################################

INST=GEN.DEPS
INST_INC=$(INST)/include
INST_LIB=$(INST)/lib
INST_BIN=$(INST)/bin

PKGDIR=GEN.PKGS
NUGET ?= nuget.exe
ifneq ($(shell $(NUGET) help 2>/dev/null; echo $$?),0)
	NUGET := /usr/src/build-extra/nuget/nuget.exe
endif

##################################################################
all:	unpack expat libssh libssh_redist curl curl_redist openssl zlib \
	libiconv libiconv_redist

unpack:
	[ -d $(PKGDIR) ] || mkdir $(PKGDIR)
	"$(NUGET)" restore packages.config -ConfigFile nuget.config \
		-NonInteractive -OutputDirectory $(PKGDIR)

insdir:
	[ -d $(INST) ]     || mkdir $(INST)
	[ -d $(INST_INC) ] || mkdir $(INST_INC)
	[ -d $(INST_BIN) ] || mkdir $(INST_BIN)
	[ -d $(INST_LIB) ] || mkdir $(INST_LIB)

##################################################################
## We place the expat headers in their own subdirectory.
## The custom is to reference <expat.h>, so compile with:
##     -I$(INST_INC)/expat

EXPAT_VER=2.1.0.11
EXPAT_ROOT=$(PKGDIR)/expat.$(EXPAT_VER)/build/native
EXPAT_INC=$(EXPAT_ROOT)/include
EXPAT_LIB=$(EXPAT_ROOT)/lib/v110/x64/Release/dynamic/utf8

expat:	insdir
	[ -d $(INST_INC)/expat ] || mkdir $(INST_INC)/expat
	cp -r $(EXPAT_INC)/* $(INST_INC)/expat/
	cp -r $(EXPAT_LIB)/* $(INST_LIB)/


##################################################################

LIBICONV_VER=1.14.0.11
LIBICONV_ROOT=$(PKGDIR)/libiconv.$(LIBICONV_VER)/build/native
LIBICONV_INC=$(LIBICONV_ROOT)/include
LIBICONV_LIB=$(LIBICONV_ROOT)/lib/v110/x64/Release/dynamic/cdecl

libiconv:	insdir
	cp -r $(LIBICONV_INC)/* $(INST_INC)/
	cp -r $(LIBICONV_LIB)/libiconv.lib $(INST_LIB)/iconv.lib


LIBICONV_REDIST_ROOT=$(PKGDIR)/libiconv.redist.$(LIBICONV_VER)/build/native
LIBICONV_REDIST_BIN=$(LIBICONV_REDIST_ROOT)/bin/v110/x64/Release/dynamic/cdecl

libiconv_redist:	insdir
	cp -r $(LIBICONV_REDIST_BIN)/* $(INST_BIN)/


##################################################################

LIBSSH_VER=1.4.3.3
LIBSSH_ROOT=$(PKGDIR)/libssh2.$(LIBSSH_VER)/build/native
LIBSSH_INC=$(LIBSSH_ROOT)/include
LIBSSH_LIB=$(LIBSSH_ROOT)/lib/v110/x64/Release/dynamic/cdecl

libssh:	insdir
	[ -d $(INST_INC)/libssh2 ] || mkdir $(INST_INC)/libssh2
	cp -r $(LIBSSH_INC)/* $(INST_INC)/libssh2
	cp -r $(LIBSSH_LIB)/* $(INST_LIB)/


LIBSSH_REDIST_ROOT=$(PKGDIR)/libssh2.redist.$(LIBSSH_VER)/build/native
LIBSSH_REDIST_BIN=$(LIBSSH_REDIST_ROOT)/bin/v110/x64/Release/dynamic/cdecl

libssh_redist:	insdir
	cp -r $(LIBSSH_REDIST_BIN)/* $(INST_BIN)/


##################################################################
## We place the curl headers in their own subdirectory.
## The custom is to reference <curl/curl.h>, so compile with:
##     -I$(INST_INC)

CURL_VER=7.30.0.2
CURL_ROOT=$(PKGDIR)/curl.$(CURL_VER)/build/native
CURL_INC=$(CURL_ROOT)/include/curl
CURL_LIB=$(CURL_ROOT)/lib/v110/x64/Release/dynamic

curl:	insdir
	[ -d $(INST_INC)/curl ] || mkdir $(INST_INC)/curl
	cp -r $(CURL_INC)/* $(INST_INC)/curl
	cp -r $(CURL_LIB)/* $(INST_LIB)/


CURL_REDIST_ROOT=$(PKGDIR)/curl.redist.$(CURL_VER)/build/native
CURL_REDIST_BIN=$(CURL_REDIST_ROOT)/bin/v110/x64/Release/dynamic

curl_redist:	insdir
	cp -r $(CURL_REDIST_BIN)/* $(INST_BIN)/


##################################################################
## We place the openssl headers in their own subdirectory.
## The custom is to reference <openssl/sha.h>, so compile with:
##     -I$(INST_INC)

OPENSSL_VER=1.0.2.1
OPENSSL_ROOT=$(PKGDIR)/openssl.v140.windesktop.msvcstl.dyn.rt-dyn.x64.$(OPENSSL_VER)
OPENSSL_INC=$(OPENSSL_ROOT)/build/native/include/openssl
OPENSSL_LIB=$(OPENSSL_ROOT)/lib/native/v140/windesktop/msvcstl/dyn/rt-dyn/x64/release

openssl:	insdir
	[ -d $(INST_INC)/openssl ] || mkdir $(INST_INC)/openssl
	cp -r $(OPENSSL_INC)/*     $(INST_INC)/openssl
	cp -r $(OPENSSL_LIB)/*.lib $(INST_LIB)/
	cp -r $(OPENSSL_LIB)/*.dll $(INST_BIN)/
	cp -r $(OPENSSL_LIB)/*.pdb $(INST_BIN)/

##################################################################
## We place the zlib headers in their own subdirectory.
## The custom is to reference <zlib.h>, so compile with:
##     -I$(INST_INC)/zlib

ZLIB_VER=1.2.8.8
ZLIB_ROOT=$(PKGDIR)/zlib.v140.windesktop.msvcstl.dyn.rt-dyn.$(ZLIB_VER)
ZLIB_INC=$(ZLIB_ROOT)/build/native/include
ZLIB_LIB=$(ZLIB_ROOT)/lib/native/v140/windesktop/msvcstl/dyn/rt-dyn/x64/RelWithDebInfo

zlib:	insdir
	[ -d $(INST_INC)/zlib ] || mkdir $(INST_INC)/zlib
	cp -r $(ZLIB_INC)/*     $(INST_INC)/zlib
	cp -r $(ZLIB_LIB)/*.lib $(INST_LIB)/
	cp -r $(ZLIB_LIB)/*.dll $(INST_BIN)/
	cp -r $(ZLIB_LIB)/*.pdb $(INST_BIN)/

##################################################################
clean:
	rm -rf $(INST)

clobber:	clean
	rm -rf $(PKGDIR)
