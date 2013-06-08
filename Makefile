LDLIBS += -lGL -lGLEW -lglut
CPPFLAGS += -MMD

CPPFLAGS += -g -O3
CFLAGS += -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-function -Wextra -Wall

CFLAGS = -std=gnu99

TARGETS = fit render_terrain

all: $(TARGETS)

CPPFLAGS += -I/usr/include/opencv2
LDLIBS   += -lopencv_imgproc -lopencv_highgui -lopencv_core

render_terrain: render_terrain.o render_terrain_show.o points_of_interest.o fltk_annotated_image.o
render_terrain: LDLIBS += -lfltk

render_terrain.o: vertex.glsl.h fragment.glsl.h

%.glsl.h: %_header.glsl %.glsl
	sed 's/.*/"&\\n"/g' $^ > $@

features_generated.h: CA_Features_20130404.txt parse_features.pl
	./parse_features.pl $< > $@

points_of_interest.o: features_generated.h

clean:
	rm -rf $(TARGETS) features_generated.h .o *.glsl.h *.d

.PHONY: clean

-include *.d
