include choose_mrbuild.mk
include $(MRBUILD_MK)/Makefile.common.header

PROJECT_NAME := horizonator
ABI_VERSION  := 0
TAIL_VERSION := 1

LDLIBS += \
  -lGLU -lGL -lepoxy -lglut \
  -lfreeimage \
  -lm \
  -pthread

# for the annotator
LDLIBS += -lcairo -lswscale

standalone.o: CFLAGS += -O0 # workaround for gcc bug. Need to creduce

CFLAGS    += --std=gnu99
CCXXFLAGS += -Wno-missing-field-initializers

################# library ###############
LIB_SOURCES += horizonator-lib.c dem.c annotator.c
horizonator-lib.o: vertex.glsl.h geometry.glsl.h fragment.glsl.h
%.glsl.h: %.glsl
	sed 's/.*/"&\\n"/g' $^ > $@.tmp && mv $@.tmp $@
EXTRA_CLEAN += *.glsl.h


################# standalone tool ###############
BIN_SOURCES += standalone.c

############### fltk tool #####################
BIN_SOURCES += horizonator.cc
FLORB_SOURCES := $(wildcard			\
                     florb/*.cpp		\
                     florb/*.cc			\
                     florb/Fl/*.cpp)
FLORB_OBJECTS   := $(addsuffix .o,$(basename $(FLORB_SOURCES)))

horizonator: $(FLORB_OBJECTS) slippymap-annotations.o

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

EXTRA_CLEAN += florb/*.o florb/Fl/*.o

horizonator.o slippymap-annotations.o $(FLORB_OBJECTS): CXXFLAGS += $(CXXFLAGS_FLORB)
horizonator: LDLIBS += $(LDLIBS_FLORB) -lfltk_gl -lfltk -lX11

florb/orb_mapctrl.o:   CXXFLAGS += -Wno-empty-body
florb/orb_tilecache.o: CXXFLAGS += -Wno-unused-parameter

annotator.o: CFLAGS += -I/usr/include/cairo


############### python #####################
# In the python api I have to cast a PyCFunctionWithKeywords to a PyCFunction,
# and the compiler complains. But that's how Python does it! So I tell the
# compiler to chill
horizonator-pywrap.o: CFLAGS += -Wno-cast-function-type
# similarly, with gcc-14 I get this without turning off this warning:
#   /usr/include/python3.12/object.h:142:9: error: initialization of 'long int' from 'void *' makes integer from pointer without a cast [-Wint-conversion]
horizonator-pywrap.o: CFLAGS += -Wno-int-conversion

horizonator-pywrap.o: CFLAGS += $(PY_MRBUILD_CFLAGS)
horizonator-pywrap.o: $(addsuffix .h,$(wildcard *.docstring))

horizonator$(PY_EXT_SUFFIX): horizonator-pywrap.o libhorizonator.so
	$(PY_MRBUILD_LINKER) $(PY_MRBUILD_LDFLAGS) $^ -o $@

DIST_PY3_MODULES := horizonator$(PY_EXT_SUFFIX)

all: horizonator$(PY_EXT_SUFFIX)

include $(MRBUILD_MK)/Makefile.common.footer

