LDLIBS += -lGL -lGLEW -lglut
CFLAGS = -std=gnu99 -g -O3

readhorizon: readhorizon.o

CFLAGS += -I/usr/include/opencv2
LDLIBS += -lopencv_imgproc -lopencv_highgui -lopencv_core

clean:
	rm -rf readhorizon *.o

.PHONY: clean

