BUILD_ROOT ?= $(HOME)/.cache/nx-gamehide
DEVKITPRO ?= /opt/devkitpro
LIBNX := $(BUILD_ROOT)/deps/libnx-install
ATMOSPHERE_SOURCE := $(BUILD_ROOT)/deps/Atmosphere
ATMOSPHERE_LIB := $(ATMOSPHERE_SOURCE)/libraries/libstratosphere/lib/nintendo_nx_arm64_armv8a/release/libstratosphere.a
export BUILD_ROOT
export PYTHONDONTWRITEBYTECODE := 1
export DEVKITPRO LIBNX ATMOSPHERE_SOURCE ATMOSPHERE_LIB
export PATH := $(DEVKITPRO)/devkitA64/bin:$(DEVKITPRO)/tools/bin:$(PATH)
