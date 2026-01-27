CXX ?= c++

EXE = RaidBuilder
SRCS = src/main.cpp src/block_sml.cpp
ifeq ($(shell uname),Darwin)
SRCS += src/mac_menu.mm
endif
OBJS = $(patsubst %.cpp,%.o,$(SRCS))
OBJS := $(patsubst %.mm,%.o,$(OBJS))

CXXFLAGS = -std=c++11 -Iinclude -I../VoxelEngine/include -I../SMLParser/include -I../SMLUI/include -I../SMLUI/imgui -I../SMLUI/imgui/backends -O2 -Wall -MMD -MP
CXXFLAGS += $(shell pkg-config --cflags glfw3 vulkan)
DEPS = $(OBJS:.o=.d)

LDFLAGS = -L../VoxelEngine -L../SMLParser -L../SMLUI
LDLIBS = -lVoxelEngine -lSMLParser -lSMLUI
LDLIBS += $(shell pkg-config --libs glfw3 vulkan)
ifeq ($(shell uname),Darwin)
LDFLAGS += -framework Cocoa
endif

SHADER_DIR = shaders
SHADERS = $(SHADER_DIR)/world.vert $(SHADER_DIR)/world.frag $(SHADER_DIR)/pick.vert $(SHADER_DIR)/pick.frag
SPV = $(SHADERS:=.spv)

all: $(EXE)

BLOCK_SRC_DIR = assets/blocks
BLOCK_OUT_DIR = build/blocks_cache
BLOCK_SMLS = $(wildcard $(BLOCK_SRC_DIR)/*.sml)
BLOCK_OUTS = $(patsubst $(BLOCK_SRC_DIR)/%.sml,$(BLOCK_OUT_DIR)/%.glb,$(BLOCK_SMLS))

BlockBake: tools/block_bake.cpp src/block_sml.cpp ../SMLParser/libSMLParser.a
	$(CXX) $(CXXFLAGS) -I../SMLParser/include -o $@ tools/block_bake.cpp src/block_sml.cpp -L../SMLParser -lSMLParser

bake-blocks: BlockBake $(BLOCK_SMLS)
	mkdir -p $(BLOCK_OUT_DIR)
	for f in $(BLOCK_SMLS); do \
	  name=$$(basename $$f .sml); \
	  ./BlockBake $$f $(BLOCK_OUT_DIR)/$$name.glb; \
	done

../VoxelEngine/libVoxelEngine.a: FORCE
	$(MAKE) -C ../VoxelEngine

../SMLParser/libSMLParser.a: FORCE
	$(MAKE) -C ../SMLParser

../SMLUI/libSMLUI.a: FORCE
	$(MAKE) -C ../SMLUI

$(EXE): ../VoxelEngine/libVoxelEngine.a ../SMLParser/libSMLParser.a ../SMLUI/libSMLUI.a $(SPV) $(OBJS) bake-blocks
	$(CXX) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

%.spv: %
	glslc -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
%.o: %.mm
	$(CXX) $(CXXFLAGS) -c -o $@ $<

-include $(DEPS)

clean:
	rm -f $(EXE) $(OBJS) $(SPV) $(DEPS) BlockBake
	rm -rf $(BLOCK_OUT_DIR)
	$(MAKE) -C ../VoxelEngine clean
	$(MAKE) -C ../SMLParser clean
	$(MAKE) -C ../SMLUI clean

FORCE:
