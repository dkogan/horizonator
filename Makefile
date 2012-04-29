LDLIBS += -lGL -lGLEW -lglut
CFLAGS = -std=gnu99 -g -O3

readhorizon: readhorizon.o

clean:
	rm -rf readhorizon *.o

.PHONY: clean

