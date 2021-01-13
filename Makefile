include /usr/include/mrbuild/Makefile.common.header

PROJECT_NAME := horizonator
ABI_VERSION  := 0
TAIL_VERSION := 0

LIB_SOURCES += dem.c render_terrain.c

BIN_SOURCES += horizonator.c

LDLIBS += \
  -lGLU -lGL -lepoxy -lglut \
  -lfreeimage \
  -lm \
  -pthread

render_terrain.o: vertex.textured.glsl.h fragment.textured.glsl.h vertex.colored.glsl.h fragment.colored.glsl.h

%.glsl.h: %.glsl
	sed 's/.*/"&\\n"/g' $^ > $@.tmp && mv $@.tmp $@

EXTRA_CLEAN += *.glsl.h

include /usr/include/mrbuild/Makefile.common.footer
