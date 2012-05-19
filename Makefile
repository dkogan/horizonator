LDLIBS += -lGL -lGLEW -lglut
CFLAGS = -std=gnu99 -g -O3

all: readhorizon

CFLAGS += -I/usr/include/opencv2
LDLIBS += -lopencv_imgproc -lopencv_highgui -lopencv_core

clean:
	rm -rf fit readhorizon *.o

.PHONY: clean

