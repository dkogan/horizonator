include /usr/include/mrbuild/Makefile.common.header

PROJECT_NAME := horizonator
ABI_VERSION  := 0
TAIL_VERSION := 1

VERSION := $(ABI_VERSION).$(TAIL_VERSION)

LDLIBS += \
  -lGLU -lGL -lepoxy -lglut \
  -lfreeimage \
  -lm \
  -pthread

################# standalone tool ###############
BIN_SOURCES += standalone.c

standalone: horizonator-lib.o dem.o

CFLAGS += --std=gnu99 -Wno-missing-field-initializers

horizonator-lib.o: vertex.glsl.h geometry.glsl.h fragment.glsl.h

%.glsl.h: %.glsl
	sed 's/.*/"&\\n"/g' $^ > $@.tmp && mv $@.tmp $@
EXTRA_CLEAN += *.glsl.h




############### fltk tool #####################
BIN_SOURCES += horizonator.cc
FLORB_SOURCES := $(wildcard			\
                     florb/*.cpp		\
                     florb/*.cc			\
                     florb/Fl/*.cpp)
FLORB_OBJECTS   := $(addsuffix .o,$(basename $(FLORB_SOURCES)))

horizonator: $(FLORB_OBJECTS) horizonator-lib.o dem.o

LDLIBS_FLORB := \
 $(shell fltk-config --use-images --ldflags) \
 $(shell curl-config --libs) \
 $(shell pkg-config --libs libpng) \
 $(shell pkg-config --libs tinyxml) \
 -lboost_filesystem \
 -lboost_system \
 -lboost_thread \
 -pthread

CXXFLAGS_FLORB := \
 -Iflorb -Iflorb/Fl \
 $(shell fltk-config --use-images --cxxflags) \
 $(shell curl-config --cflags) \
 $(shell pkg-config --cflags libpng) \
 $(shell pkg-config --cflags tinyxml)

horizonator.o $(FLORB_OBJECTS): CXXFLAGS += $(CXXFLAGS_FLORB)
horizonator: LDLIBS += $(LDLIBS_FLORB) -lfltk_gl

florb/orb_mapctrl.o:   CXXFLAGS += -Wno-empty-body
florb/orb_tilecache.o: CXXFLAGS += -Wno-unused-parameter

include /usr/include/mrbuild/Makefile.common.footer
