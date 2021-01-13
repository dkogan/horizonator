include /usr/include/mrbuild/Makefile.common.header

PROJECT_NAME := horizonator
ABI_VERSION  := 0
TAIL_VERSION := 1

VERSION := $(ABI_VERSION).$(TAIL_VERSION)

BIN_SOURCES += horizonator.c
horizonator: dem.o render_terrain.o

LDLIBS += \
  -lGLU -lGL -lepoxy -lglut \
  -lfreeimage \
  -lm \
  -pthread

CFLAGS += --std=gnu99

render_terrain.o: vertex.textured.glsl.h fragment.textured.glsl.h vertex.colored.glsl.h fragment.colored.glsl.h

%.glsl.h: %.glsl
	sed 's/.*/"&\\n"/g' $^ > $@.tmp && mv $@.tmp $@

EXTRA_CLEAN += *.glsl.h

include /usr/include/mrbuild/Makefile.common.footer
