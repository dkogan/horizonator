LDLIBS += -lGL -lGLEW -lglut
CFLAGS = -std=gnu99

readhorizon: readhorizon.o

clean:
	rm -rf readhorizon *.o

.PHONY: clean

