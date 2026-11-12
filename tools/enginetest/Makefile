# Runs the modules against Rack's real engine, headless. Needs Rack's source tree (with its nanovg, nanosvg,
# oui-blendish, osdialog, simde, filesystem, pffft, tinyexpr and fuzzysearchdatabase submodules) and the system's
# glfw, glew, jansson, curl, libarchive, speexdsp and samplerate. Usage: make RACK_SRC=/path/to/Rack && ./test
RACK_SRC ?= ../../../vcvrack/rack
R := $(RACK_SRC)
P := ../..
D := $(R)/dep
INC := -I$(R)/include -I$(D)/nanovg/src -I$(D)/nanovg/example -I$(D)/nanosvg/src -I$(D)/oui-blendish -I$(D)/osdialog \
	-I$(D)/pffft -I$(D)/simde -I$(D)/tinyexpr -I$(D)/fuzzysearchdatabase/src -I$(D)/filesystem/include
FLAGS := -O1 -g -DARCH_LIN -DARCH_X64 -march=nehalem -funsafe-math-optimizations $(INC)

# libRack without its audio and MIDI drivers
RACK_CPP := $(filter-out $(R)/src/rtaudio.cpp $(R)/src/rtmidi.cpp,$(wildcard $(R)/src/*.cpp $(R)/src/*/*.cpp))
RACK_C := $(D)/nanovg/src/nanovg.c $(D)/osdialog/osdialog.c $(D)/osdialog/osdialog_zenity.c $(D)/oui-blendish/blendish.c \
	$(D)/pffft/pffft.c $(D)/pffft/fftpack.c $(D)/tinyexpr/tinyexpr.c
RACK_OBJ := $(patsubst $(R)/%,build/%.o,$(RACK_CPP) $(RACK_C))

SRC := test.cpp setup.cpp $(wildcard $(P)/src/*.cpp) $(wildcard $(P)/src/core/*.cpp $(P)/src/kit/*.cpp)

test: $(SRC) $(wildcard $(P)/src/*.cpp $(P)/src/*/*.hpp) build/libRack.a
	$(CXX) $(FLAGS) -std=c++17 -Wall -Wno-unused-parameter -I$(P)/src $(SRC) -o $@ build/libRack.a \
		-lglfw -lGLEW -lGL -ljansson -lcurl -larchive -lzstd -lspeexdsp -lsamplerate -lpthread -ldl -lX11

build/libRack.a: $(RACK_OBJ)
	ar rcs $@ $^
build/%.cpp.o: $(R)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++11 $(FLAGS) -fPIC -w -D_RACK_VERSION=2 -c $< -o $@
build/%.c.o: $(R)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -fPIC -w -DTE_POW_FROM_RIGHT -DTE_NAT_LOG -c $< -o $@

clean:
	rm -rf build test
.PHONY: clean
