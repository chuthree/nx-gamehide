include ../scripts/build.mk
include $(LIBNX)/switch_rules
CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++
TARGET := nx-gamehide
BUILD := $(BUILD_ROOT)/backend
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
DEFINES := -DATMOSPHERE -DATMOSPHERE_ARCH_ARM64 -DATMOSPHERE_ARCH_ARM_V8A \
 -DATMOSPHERE_BOARD_NINTENDO_NX -DATMOSPHERE_OS_HORIZON -D__SWITCH__ \
 -DATMOSPHERE_IS_STRATOSPHERE -D_GNU_SOURCE
INCLUDES := -I../common -isystem $(ATMOSPHERE_SOURCE)/libraries/libstratosphere/include -isystem $(ATMOSPHERE_SOURCE)/libraries/libvapours/include -isystem $(LIBNX)/include
CXXFLAGS := -g -Wall -Wextra -Werror -Wno-missing-field-initializers -O2 -ffunction-sections -fdata-sections -fno-strict-aliasing -fwrapv \
 -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector \
 -Wno-format-zero-length -Wno-invalid-offsetof $(ARCH) $(INCLUDES) $(DEFINES) \
 -std=gnu++23 -fno-rtti -fno-exceptions
# Use the upstream stratosphere linker script alongside libnx startup rules.
export ATMOSPHERE_LIBRARIES_DIR := $(BUILD)/linker-root
WRAPS := __cxa_pure_virtual __cxa_throw __cxa_rethrow __cxa_allocate_exception \
 __cxa_free_exception __cxa_begin_catch __cxa_end_catch __cxa_call_unexpected \
 __cxa_call_terminate __gxx_personality_v0 _Unwind_Resume _ZSt19__throw_logic_errorPKc \
 _ZSt20__throw_length_errorPKc _ZNSt11logic_errorC2EPKc exit
REQUIRED := __libnx_initheap __libnx_exception_handler __libnx_alloc __libnx_aligned_alloc \
 __libnx_free __appInit __appExit argvSetup
LDFLAGS := -specs=$(CURDIR)/linker/stratosphere.specs -specs=$(LIBNX)/switch.specs \
 -g $(ARCH) $(foreach s,$(WRAPS),-Wl,--wrap,$(s)) $(foreach s,$(REQUIRED),-Wl,--require-defined,$(s)) \
 -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,-z,relro,-z,now
LIBS := $(ATMOSPHERE_LIB) -L$(LIBNX)/lib -lnx
.PHONY: all clean
all: $(BUILD)/main $(BUILD)/main.npdm $(BUILD)/exefs.nsp
$(BUILD)/main.o: source/main.cpp ../common/protocol.hpp ../common/filter.hpp Makefile ../scripts/build.mk
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/$(TARGET).elf: $(BUILD)/main.o linker/stratosphere.specs linker/discard-ehframe.ld $(ATMOSPHERE_LIB) $(LIBNX)/lib/libnx.a $(LIBNX)/switch.specs $(LIBNX)/switch.ld
	@mkdir -p $(BUILD)/linker-root/libstratosphere
	@cp linker/discard-ehframe.ld $(BUILD)/linker-root/libstratosphere/
	$(CXX) $(LDFLAGS) -o $@ $< $(LIBS)
$(BUILD)/main: $(BUILD)/$(TARGET).elf
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
$(BUILD)/main.npdm: backend.json
	@mkdir -p $(BUILD)
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
$(BUILD)/exefs.nsp: $(BUILD)/main $(BUILD)/main.npdm
	@mkdir -p $(BUILD)/exefs
	@cp $(BUILD)/main $(BUILD)/main.npdm $(BUILD)/exefs/
	$(DEVKITPRO)/tools/bin/build_pfs0 $(BUILD)/exefs $@
clean:
	rm -rf $(BUILD)
-include $(BUILD)/main.d
