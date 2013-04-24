LDLIBS += -lGL -lGLEW -lglut
CFLAGS = -std=gnu99 -g -O3

CFLAGS += -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-function -Wextra -Wall

TARGETS = fit render_terrain

all: $(TARGETS)

CFLAGS += -I/usr/include/opencv2
LDLIBS += -lopencv_imgproc -lopencv_highgui -lopencv_core

render_terrain: render_terrain.o render_terrain_show.o

render_terrain.o: vertex.glsl.h fragment.glsl.h

%.glsl.h: %_header.glsl %.glsl
	sed 's/.*/"&\\n"/g' $^ > $@

clean:
	rm -rf $(TARGETS) *.o *.glsl.h

.PHONY: clean

