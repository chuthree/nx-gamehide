include ../scripts/build.mk
include $(LIBNX)/switch_rules
TARGET := nx-gamehide
BUILD := $(BUILD_ROOT)/overlay
OUTPUT := $(BUILD)/$(TARGET)
VERSION := $(shell sed -n 's/.*Version = "\([^"]*\)";.*/\1/p' ../common/protocol.hpp)
CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CXXFLAGS := -std=gnu++20 -g -O2 -Wall -Wextra -Werror -fno-exceptions \
 -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__ -D_GNU_SOURCE \
 -I../common -isystem libtesla/include -isystem $(LIBNX)/include
LDFLAGS := -specs=$(LIBNX)/switch.specs $(ARCH) -g -Wl,-Map,$(BUILD)/$(TARGET).map
.PHONY: all clean
all: $(OUTPUT).ovl
$(BUILD)/main.o: source/main.cpp Makefile ../scripts/build.mk
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OUTPUT).elf: $(BUILD)/main.o $(LIBNX)/lib/libnx.a $(LIBNX)/switch.specs $(LIBNX)/switch.ld
	$(CXX) $(LDFLAGS) -o $@ $< -L$(LIBNX)/lib -lnx
$(OUTPUT).nacp: ../common/protocol.hpp Makefile
	@mkdir -p $(BUILD)
	$(DEVKITPRO)/tools/bin/nacptool --create "GameHide" "NX-GameHide contributors" "$(VERSION)" $@
$(OUTPUT).ovl: $(OUTPUT).elf $(OUTPUT).nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(OUTPUT).nacp
clean:
	rm -rf $(BUILD)
-include $(BUILD)/main.d
