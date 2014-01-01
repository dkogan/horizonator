CPPFLAGS += -MMD
CPPFLAGS += -ggdb3 -O3
CFLAGS   += -std=gnu99
CPPFLAGS += -Wno-missing-field-initializers -Wno-unused-function -Wextra -Wall

# slippy-map libraries
# I need -lX11 because of http://bugs.debian.org/713933
LDLIBS_HORIZON := \
 -lX11 \
 $(shell pkg-config --libs glu) \
 $(shell pkg-config --libs glew) \
 -lglut -lm \
 $(shell fltk-config --use-images --ldflags) \
 $(shell curl-config --libs) \
 $(shell pkg-config --libs libpng) \
 $(shell pkg-config --libs tinyxml) \
 $(shell pkg-config --libs libzip) \
 -lboost_filesystem \
 -lboost_system \
 -lboost_thread \
 -pthread

# slippy-map compile time stuff
CXXFLAGS_HORIZON := \
 -Iflorb -Iflorb/Fl \
 $(shell pkg-config --cflags glu) \
 $(shell pkg-config --cflags glew) \
 $(shell fltk-config --use-images --cxxflags) \
 $(shell curl-config --cflags) \
 $(shell pkg-config --cflags libpng) \
 $(shell pkg-config --cflags tinyxml) \
 $(shell pkg-config --cflags libzip)


TARGETS = fit render_terrain

all: $(TARGETS)

CPPFLAGS += -I/usr/include/opencv2
LDLIBS   += -lopencv_imgproc -lopencv_highgui -lopencv_core -lopencv_legacy






FLORB_SOURCES := $(wildcard $(addprefix florb/,*.cpp *.cc Fl/*.cpp))

FLORB_OBJECTS   := $(addsuffix .o,$(basename $(FLORB_SOURCES)))
HORIZON_OBJECTS := render_terrain.o render_terrain_show.o points_of_interest.o fltk_annotated_image.o dem_downloader.o orb_renderviewlayer.o Fl_Scroll_Draggable.o


render_terrain: $(HORIZON_OBJECTS) $(FLORB_OBJECTS)
render_terrain: LDLIBS   += $(LDLIBS_HORIZON)
render_terrain: CXXFLAGS += $(CXXFLAGS_HORIZON)
render_terrain: LINK.o := g++

render_terrain.o: vertex.glsl.h fragment.glsl.h

tstcanny: LDLIBS += -lfltk

%.glsl.h: %.glsl
	sed 's/.*/"&\\n"/g' $^ > $@

features_generated.h: $(wildcard *Features*.txt) parse_features.pl
	./parse_features.pl $(filter-out %.pl,$^) > $@

points_of_interest.o: features_generated.h

clean:
	rm -rf $(TARGETS) features_generated.h *.o *.glsl.h *.d florb/*.o florb/*.d florb/Fl/*.o florb/Fl/*.d

.PHONY: clean

-include *.d
