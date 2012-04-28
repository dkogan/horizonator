LDLIBS += -lGL -lGLEW -lglut
CFLAGS = -std=gnu99

objview: objview.o

clean:
	rm -rf render *.o

.PHONY: clean

