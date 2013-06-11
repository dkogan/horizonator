CXXFLAGS := -g -Wall -O2 \
	-I$(abspath .) -I$(abspath Fl) \
	$(shell fltk-config --use-images --cxxflags) \
	$(shell curl-config --cflags) \
	$(shell pkg-config --cflags libpng) \
	$(shell pkg-config --cflags tinyxml)

LDFLAGS := \
	$(shell fltk-config --use-images --ldflags) \
	$(shell curl-config --libs) \
	$(shell pkg-config --libs libpng) \
	$(shell pkg-config --libs tinyxml) \
    -lboost_filesystem \
    -lboost_system \
    -lboost_thread

slippy: $(patsubst %.cpp,%.o,$(wildcard *.cpp *.cc Fl/*.cpp))
	$(CXX) -o $@ $(LDLIBS) $(LDFLAGS) $^


# clean target
clean:
	rm -f slippy *.o Fl/*.o
.PHONY: clean
