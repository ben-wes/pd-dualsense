# library name
lib.name = dualsense

SOURCE_DIR = ./hidapi

XINCLUDE = -I ${SOURCE_DIR}/hidapi 

cflags = ${XINCLUDE} -I . -DHAVE_CONFIG_H

dslink.class.sources = dslink.c

define forLinux
	dslink.class.sources += ${SOURCE_DIR}/linux/hid.c
	ldlibs += -ludev -lrt
endef

define forWindows
	dslink.class.sources += ${SOURCE_DIR}/windows/hid.c
	XINCLUDE += -I ${SOURCE_DIR}/windows
	ldlibs += -mwindows
endef

define forDarwin
	dslink.class.sources += ${SOURCE_DIR}/mac/hid.c
	XINCLUDE += -I ${SOURCE_DIR}/mac
	ldlibs += -framework IOKit -framework CoreFoundation -framework AppKit
endef

datafiles = \
	dslink-help.pd \
	${empty}


objectsdir = ./build
PDLIBBUILDER_DIR=./pd-lib-builder
include $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder
