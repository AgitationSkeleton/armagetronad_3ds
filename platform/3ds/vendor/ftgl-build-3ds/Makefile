DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

FTGL := $(abspath ../ftgl)
PORTLIBS := $(DEVKITPRO)/portlibs/3ds
COMPAT := $(abspath ../../include)
BUILD := build
TARGET := libftgl.a

CXX := $(DEVKITARM)/bin/arm-none-eabi-g++
AR := $(DEVKITARM)/bin/arm-none-eabi-gcc-ar

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CXXFLAGS := -O2 -g -Wall -Wextra -ffunction-sections -fdata-sections \
	-fno-strict-aliasing -std=gnu++11 $(ARCH) \
	-D__3DS__ -DFTGL_LIBRARY_STATIC \
	-I$(COMPAT) \
	-I$(FTGL) \
	-I$(FTGL)/src \
	-I$(FTGL)/src/FTFont \
	-I$(FTGL)/src/FTGlyph \
	-I$(PORTLIBS)/include/freetype2 \
	-I$(PORTLIBS)/include \
	-I$(DEVKITPRO)/libctru/include

SOURCES := \
	FTCharmap.cpp \
	FTFace.cpp \
	FTGlyphContainer.cpp \
	FTLibrary.cpp \
	FTPoint.cpp \
	FTSize.cpp \
	FTGlyph.cpp \
	FTTextureGlyph.cpp \
	FTFont.cpp \
	FTTextureFont.cpp

OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.cpp=.o))

VPATH := \
	$(FTGL)/src \
	$(FTGL)/src/FTGlyph \
	$(FTGL)/src/FTFont

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(AR) rcs $@ $^

$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(BUILD)/$*.d -c $< -o $@

clean:
	rm -rf $(BUILD) $(TARGET)

-include $(OBJECTS:.o=.d)
