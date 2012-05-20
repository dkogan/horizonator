LDLIBS += -lGL -lGLEW -lglut
CFLAGS = -std=gnu99 -g -O3

CFLAGS += -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-function -Wextra -Wall

all: readhorizon

CFLAGS += -I/usr/include/opencv2
LDLIBS += -lopencv_imgproc -lopencv_highgui -lopencv_core

clean:
	rm -rf fit readhorizon *.o

.PHONY: clean

