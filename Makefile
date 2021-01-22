include /usr/include/mrbuild/Makefile.common.header

PROJECT_NAME := horizonator
ABI_VERSION  := 0
TAIL_VERSION := 1

VERSION := $(ABI_VERSION).$(TAIL_VERSION)

BIN_SOURCES += standalone.c
standalone: dem.o render_terrain.o

LDLIBS += \
  -lGLU -lGL -lepoxy -lglut \
  -lfreeimage \
  -lm \
  -pthread

CFLAGS += --std=gnu99 -Wno-missing-field-initializers

render_terrain.o: vertex.glsl.h geometry.glsl.h fragment.glsl.h

%.glsl.h: %.glsl
	sed 's/.*/"&\\n"/g' $^ > $@.tmp && mv $@.tmp $@

EXTRA_CLEAN += *.glsl.h

include /usr/include/mrbuild/Makefile.common.footer
