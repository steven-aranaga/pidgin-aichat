#!/bin/make -f
PIDGIN_TREE_TOP ?= ../pidgin-2.10.11
PIDGIN3_TREE_TOP ?= ../pidgin-main
LIBPURPLE_DIR ?= $(PIDGIN_TREE_TOP)/libpurple
WIN32_DEV_TOP ?= $(PIDGIN_TREE_TOP)/../win32-dev

WIN32_CC ?= $(WIN32_DEV_TOP)/mingw-4.7.2/bin/gcc

PKG_CONFIG ?= pkg-config
DIR_PERM = 0755
LIB_PERM = 0755
FILE_PERM = 0644
MAKENSIS ?= makensis
XGETTEXT ?= xgettext

CFLAGS	?= -O2 -g -pipe
LDFLAGS ?= 

# Do some nasty OS and purple version detection
ifeq ($(OS),Windows_NT)
  #only defined on 64-bit windows
  PROGFILES32 = ${ProgramFiles(x86)}
  ifndef PROGFILES32
    PROGFILES32 = $(PROGRAMFILES)
  endif
  PLUGIN_TARGET = libaichat.dll
  PLUGIN_DEST = "$(PROGFILES32)/Pidgin/plugins"
  PLUGIN_ICONS_DEST = "$(PROGFILES32)/Pidgin/pixmaps/pidgin/protocols"
  MAKENSIS = "$(PROGFILES32)/NSIS/makensis.exe"
else

  UNAME_S := $(shell uname -s)

  #.. There are special flags we need for OSX
  ifeq ($(UNAME_S), Darwin)
    #
    #.. /opt/local/include and subdirs are included here to ensure this compiles
    #   for folks using Macports.  I believe Homebrew uses /usr/local/include
    #   so things should "just work".  You *must* make sure your packages are
    #   all up to date or you will most likely get compilation errors.
    #
    INCLUDES = -I/opt/local/include -lz $(OS)

    CC = gcc
  else
    INCLUDES = 
    CC ?= gcc
  endif

  ifeq ($(shell $(PKG_CONFIG) --exists purple-3 2>/dev/null && echo "true"),)
    ifeq ($(shell $(PKG_CONFIG) --exists purple 2>/dev/null && echo "true"),)
      PLUGIN_TARGET = FAILNOPURPLE
      PLUGIN_DEST =
	  PLUGIN_ICONS_DEST =
    else
      PLUGIN_TARGET = libaichat.so
      PLUGIN_DEST = $(DESTDIR)`$(PKG_CONFIG) --variable=plugindir purple`
	  PLUGIN_ICONS_DEST = $(DESTDIR)`$(PKG_CONFIG) --variable=datadir purple`/pixmaps/pidgin/protocols
    endif
  else
    PLUGIN_TARGET = libaichat3.so
    PLUGIN_DEST = $(DESTDIR)`$(PKG_CONFIG) --variable=plugindir purple-3`
	PLUGIN_ICONS_DEST = $(DESTDIR)`$(PKG_CONFIG) --variable=datadir purple-3`/pixmaps/pidgin/protocols
  endif
endif

WIN32_CFLAGS = -I$(WIN32_DEV_TOP)/glib-2.28.8/include -I$(WIN32_DEV_TOP)/glib-2.28.8/include/glib-2.0 -I$(WIN32_DEV_TOP)/glib-2.28.8/lib/glib-2.0/include -I$(WIN32_DEV_TOP)/json-glib-0.14/include/json-glib-1.0 -DENABLE_NLS -DPACKAGE_VERSION='"$(PLUGIN_VERSION)"' -Wall -Wextra -Werror -Wno-deprecated-declarations -Wno-unused-parameter -fno-strict-aliasing -Wformat -Wno-sign-compare
WIN32_LDFLAGS = -L$(WIN32_DEV_TOP)/glib-2.28.8/lib -L$(WIN32_DEV_TOP)/json-glib-0.14/lib -lpurple -lintl -lglib-2.0 -lgobject-2.0 -lgio-2.0 -ljson-glib-1.0 -g -ggdb -static-libgcc -lz
WIN32_PIDGIN2_CFLAGS = -I$(PIDGIN_TREE_TOP)/libpurple -I$(PIDGIN_TREE_TOP) $(WIN32_CFLAGS)
WIN32_PIDGIN3_CFLAGS = -I$(PIDGIN3_TREE_TOP)/libpurple -I$(PIDGIN3_TREE_TOP) -I$(WIN32_DEV_TOP)/gplugin-dev/gplugin $(WIN32_CFLAGS)
WIN32_PIDGIN2_LDFLAGS = -L$(PIDGIN_TREE_TOP)/libpurple $(WIN32_LDFLAGS)
WIN32_PIDGIN3_LDFLAGS = -L$(PIDGIN3_TREE_TOP)/libpurple -L$(WIN32_DEV_TOP)/gplugin-dev/gplugin $(WIN32_LDFLAGS) -lgplugin

C_FILES = \
	libaichat.c \
	markdown.c \
	providers.c \
	provider_registry.c \
	providers/openai.c \
	providers/anthropic.c \
	providers/google.c \
	providers/openai_compat.c \
	providers/openrouter.c \
	providers/huggingface.c \
	providers/cohere.c \
	providers/ollama.c \
	providers/custom.c
PURPLE_COMPAT_FILES := purple2compat/http.c purple2compat/purple-socket.c
PURPLE_C_FILES := libaichat.c $(C_FILES)



.PHONY:	all install FAILNOPURPLE clean translations

all: $(PLUGIN_TARGET)

libaichat.so: $(PURPLE_C_FILES) $(PURPLE_COMPAT_FILES)
	$(CC) -fPIC $(CFLAGS) -shared -o $@ $^ $(LDFLAGS) `$(PKG_CONFIG) purple glib-2.0 json-glib-1.0 zlib --libs --cflags`  $(INCLUDES) -Ipurple2compat -g -ggdb

libaichat3.so: $(PURPLE_C_FILES)
	$(CC) -fPIC $(CFLAGS) -shared -o $@ $^ $(LDFLAGS) `$(PKG_CONFIG) purple-3 glib-2.0 json-glib-1.0 zlib --libs --cflags` $(INCLUDES)  -g -ggdb

libaichat.dll: $(PURPLE_C_FILES) $(PURPLE_COMPAT_FILES)
	$(WIN32_CC) -shared -o $@ $^ $(WIN32_PIDGIN2_CFLAGS) $(WIN32_PIDGIN2_LDFLAGS) -Ipurple2compat

libaichat3.dll: $(PURPLE_C_FILES)
	$(WIN32_CC) -shared -o $@ $^ $(WIN32_PIDGIN3_CFLAGS) $(WIN32_PIDGIN3_LDFLAGS)

install: $(PLUGIN_TARGET)
	mkdir -m $(DIR_PERM) -p $(PLUGIN_DEST)
	install -m $(LIB_PERM) -p $(PLUGIN_TARGET) $(PLUGIN_DEST)

install-icons: icons/16/aichat.png icons/22/aichat.png icons/48/aichat.png
	mkdir -m $(DIR_PERM) -p $(PLUGIN_ICONS_DEST)/16
	mkdir -m $(DIR_PERM) -p $(PLUGIN_ICONS_DEST)/22
	mkdir -m $(DIR_PERM) -p $(PLUGIN_ICONS_DEST)/48
	install -m $(FILE_PERM) -p icons/16/aichat.png $(PLUGIN_ICONS_DEST)/16/aichat.png
	install -m $(FILE_PERM) -p icons/22/aichat.png $(PLUGIN_ICONS_DEST)/22/aichat.png
	install -m $(FILE_PERM) -p icons/48/aichat.png $(PLUGIN_ICONS_DEST)/48/aichat.png

installer: purple-aichat.nsi libaichat.dll
	$(MAKENSIS) "/DPIDGIN_VARIANT"="Pidgin" "/DPRODUCT_NAME"="purple-aichat" "/DINSTALLER_NAME"="purple-aichat-installer" "/DJSON_GLIB_DLL"="libjson-glib-1.0.dll" purple-aichat.nsi

translations: po/purple-aichat.pot

po/purple-aichat.pot: $(PURPLE_C_FILES)
	$(XGETTEXT) $^ -k_ --no-location -o $@

po/%.po: po/purple-aichat.pot
	msgmerge $@ po/purple-aichat.pot > tmp-$*
	mv -f tmp-$* $@

po/%.mo: po/%.po
	msgfmt -o $@ $^

%-locale-install: po/%.mo
	install -D -m $(FILE_PERM) -p po/$(*F).mo $(LOCALEDIR)/$(*F)/LC_MESSAGES/purple-aichat.mo
	
FAILNOPURPLE:
	echo "You need libpurple development headers installed to be able to compile this plugin"

clean:
	rm -f $(PLUGIN_TARGET)
