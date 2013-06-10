# Compiler and linker flags
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

.SECONDARY: $(patsubst %.fl,%.cpp,$(wildcard fluid/*.fl))

florb: $(patsubst %.cpp,%.o,$(wildcard *.cpp)) $(patsubst %.fl,%.o,$(wildcard fluid/*.fl)) $(patsubst %.cpp,%.o,$(wildcard Fl/*.cpp))
	$(CXX) -o $@ $(LDLIBS) $(LDFLAGS) $^

# Rule for generating CPP code from fluid sources
%.hpp %.cpp: %.fl
	fluid -c -o $*.cpp -h $(@:%.cpp=%.hpp) $^


fluid/orb_ui.o: | fluid/orb_layers.hpp fluid/orb_dlg_settings.hpp fluid/orb_about.hpp


# clean target
clean:
	rm -f florb *.o $(patsubst %.cpp,%.o,$(wildcard *.cpp)) fluid/*.cpp fluid/*.hpp fluid/*.o Fl/*.o
.PHONY: clean
