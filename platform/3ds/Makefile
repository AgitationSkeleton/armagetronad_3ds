#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM to the devkitARM directory")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := armagetronad-3ds
BUILD       := build-full
ROMFS       := romfs

SOURCES := \
	source \
	../../src/tools \
	../../src/tools/values \
	../../src/resource \
	../../src/network \
	../../src/engine \
	../../src/engine/sound/sdl_mixer \
	../../src/render \
	../../src/ui \
	../../src/tron \
	../../src/tron/cockpit \
	../../src/tron/zone \
	../../src/thirdparty/binreloc \
	../../src/thirdparty/mathexpr \
	../../src/thirdparty/particles/ParticleDLL \
	../../src/thirdparty/scrap \
	generated

INCLUDES := \
	include \
	../../src \
	../../src/tools \
	../../src/tools/values \
	../../src/resource \
	../../src/network \
	../../src/engine \
	../../src/engine/sound \
	../../src/engine/sound/sdl_mixer \
	../../src/render \
	../../src/ui \
	../../src/tron \
	../../src/tron/cockpit \
	../../src/tron/zone \
	../../src/thirdparty/binreloc \
	../../src/thirdparty/mathexpr \
	../../src/thirdparty/utf8 \
	../../src/thirdparty/particles \
	../../src/thirdparty/particles/ParticleDLL \
	../../src/thirdparty/scrap \
	generated \
	vendor/boost \
	vendor/ftgl/src \
	vendor/protobuf/src \
	vendor/libxml2/include \
	vendor/libxml2-build-3ds

APP_TITLE       := Armagetron Advanced 3DS
APP_DESCRIPTION := Nintendo 3DS homebrew port
APP_AUTHOR      := Armagetron Advanced contributors
APP_ICON        := $(TOPDIR)/../../desktop/icons/48x48/armagetronad.png

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

DEFINES := \
	-D__3DS__ \
	-DHAVE_LIBSDL=1 \
	-DHAVE_LIBSDL_IMAGE=1 \
	-DHAVE_SDL_SDL_IMAGE_H=1 \
	-DHAVE_LIBSDL_MIXER=1 \
	-DHAVE_LIBXML2=1 \
	-DHAVE_LIBCURL=1 \
	-DHAVE_FTGL_FTGL_H=1 \
	-DLIBCURL_PROTOCOL_HTTP=1 \
	-DNO_MALLOC_REPLACEMENT=1

CFLAGS := -g -Wall -Wextra -O2 -mword-relocations -ffunction-sections \
	-fdata-sections -fno-strict-aliasing $(ARCH)
CFLAGS += $(INCLUDE) $(DEFINES)
CXXFLAGS := $(CFLAGS) -std=gnu++17
ASFLAGS := -g $(ARCH)
LDFLAGS := -specs=3dsx.specs -g $(ARCH) \
	-Wl,-Map,$(notdir $*.map),--gc-sections \
	-Wl,--wrap=C3D_Init

PORTLIBS := $(DEVKITPRO)/portlibs/3ds
PROTOBUF_BUILD := $(CURDIR)/vendor/protobuf-build-3ds
LIBXML2_BUILD := $(CURDIR)/vendor/libxml2-build-3ds
FTGL_BUILD := $(CURDIR)/vendor/ftgl-build-3ds

LIBS := \
	-lprotobuf -lxml2 -lftgl -lfreetype -lbz2 \
	-lSDL_mixer -lSDL_image -lSDL \
	-lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
	-lmad -lvorbisidec -logg -lmikmod \
	-lpng -ljpeg -lz \
	-lcitro3d -lctru -lm

LIBDIRS := \
	$(PROTOBUF_BUILD) \
	$(LIBXML2_BUILD) \
	$(FTGL_BUILD) \
	$(PORTLIBS) \
	$(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CCFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cc)))
CPPFILES := $(filter-out \
	main.cpp \
	memtest.cpp transfab.cpp tSafePTRBase.cpp \
	astat.cpp l2_demo.cpp l3_demo.cpp master.cpp nettest.cpp nettest_main.cpp \
	eSound.cpp test.cpp \
	rConsoleCout.cpp testgl.cpp \
	uOSXPaste.cpp \
	gWinZone.cpp \
	example.cpp example_c.cpp mathexpr_c.cpp,$(CPPFILES))
SFILES :=
PICAFILES := aa_gl_vshader.v.pica

export LD := $(CXX)
export OFILES_SOURCES := \
	$(CPPFILES:.cpp=.o) \
	$(CCFILES:.pb.cc=_pb.o) \
	$(CFILES:.c=.o) \
	$(SFILES:.s=.o)
export OFILES_BIN := $(PICAFILES:.v.pica=.shbin.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES := $(PICAFILES:.v.pica=_shbin.h)

export INCLUDE := \
	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
	-I$(PORTLIBS)/include/SDL \
	-I$(PORTLIBS)/include/freetype2 \
	-I$(PORTLIBS)/include \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
	-I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
	-L$(PROTOBUF_BUILD) -L$(LIBXML2_BUILD) -L$(FTGL_BUILD)

export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)

.PHONY: all clean dependencies

all: dependencies $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

dependencies:
	@test -f $(PROTOBUF_BUILD)/libprotobuf.a
	@test -f $(LIBXML2_BUILD)/libxml2.a
	@test -f $(FTGL_BUILD)/libftgl.a

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf

else

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES): $(HFILES)

%_pb.o: %.pb.cc
	$(SILENTMSG) $(notdir $<)
	$(ADD_COMPILE_COMMAND) add $(CXX) "$(_EXTRADEFS) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@" $<
	$(SILENTCMD)$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(_EXTRADEFS) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@ $(ERROR_FILTER)

$(OUTPUT).elf: $(OFILES)

.PRECIOUS: %.shbin

%.shbin.o %_shbin.h: %.shbin
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

-include $(DEPSDIR)/*.d

endif
