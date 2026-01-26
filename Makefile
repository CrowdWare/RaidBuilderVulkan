CXX ?= c++

EXE = RaidBuilder
SRCS = src/main.cpp
OBJS = $(SRCS:.cpp=.o)

CXXFLAGS = -std=c++11 -Iinclude -I../VoxelEngine/include -I../SMLParser/include -I../SMLUI/include -I../SMLUI/imgui -I../SMLUI/imgui/backends -O2 -Wall -MMD -MP
CXXFLAGS += $(shell pkg-config --cflags glfw3 vulkan)
DEPS = $(OBJS:.o=.d)

LDFLAGS = -L../VoxelEngine -L../SMLParser -L../SMLUI
LDLIBS = -lVoxelEngine -lSMLParser -lSMLUI
LDLIBS += $(shell pkg-config --libs glfw3 vulkan)

SHADER_DIR = shaders
SHADERS = $(SHADER_DIR)/world.vert $(SHADER_DIR)/world.frag $(SHADER_DIR)/pick.vert $(SHADER_DIR)/pick.frag
SPV = $(SHADERS:=.spv)

all: $(EXE)

../VoxelEngine/libVoxelEngine.a: FORCE
	$(MAKE) -C ../VoxelEngine

../SMLParser/libSMLParser.a: FORCE
	$(MAKE) -C ../SMLParser

../SMLUI/libSMLUI.a: FORCE
	$(MAKE) -C ../SMLUI

$(EXE): ../VoxelEngine/libVoxelEngine.a ../SMLParser/libSMLParser.a ../SMLUI/libSMLUI.a $(SPV) $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

%.spv: %
	glslc -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

-include $(DEPS)

clean:
	rm -f $(EXE) $(OBJS) $(SPV) $(DEPS)
	$(MAKE) -C ../VoxelEngine clean
	$(MAKE) -C ../SMLParser clean
	$(MAKE) -C ../SMLUI clean

FORCE:
