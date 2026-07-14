CC ?= cc
KERNEL_LD ?= i686-elf-ld
ZIG ?= tools/zig/zig

HOST_OS := $(shell uname -s)

ifeq ($(HOST_OS),Darwin)
TARGET_FLAGS := -target i686-elf -m32
else
TARGET_FLAGS := -m32
endif

ifeq ($(shell if command -v $(KERNEL_LD) >/dev/null 2>&1; then echo 0; else echo 1; fi),1)
ifeq ($(shell if command -v x86_64-elf-ld >/dev/null 2>&1; then echo 0; else echo 1; fi),0)
KERNEL_LD := x86_64-elf-ld
else
ifeq ($(shell if command -v ld.lld >/dev/null 2>&1; then echo 0; else echo 1; fi),0)
KERNEL_LD := ld.lld
else
ifeq ($(shell if [ "$(HOST_OS)" != Darwin ] && command -v ld >/dev/null 2>&1; then echo 0; else echo 1; fi),0)
KERNEL_LD := ld
else
ifneq ($(wildcard $(ZIG)),)
KERNEL_LD := $(ZIG) ld.lld
endif
endif
endif
endif
endif

CFLAGS := $(TARGET_FLAGS) -Isrc -ffreestanding -fno-stack-protector -fno-pic -O2 -Wall -Wextra
ASFLAGS := $(TARGET_FLAGS) -ffreestanding
LDFLAGS := -m elf_i386 -T linker.ld --build-id=none

BUILD_DIR := build
SRC_DIR := src

OBJS := $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/kernel_text.o $(BUILD_DIR)/kernel_format.o $(BUILD_DIR)/yBash.o $(BUILD_DIR)/terminal_driver_legacy.o $(BUILD_DIR)/virtualfs.o

.PHONY: all clean

all: $(BUILD_DIR)/kernel.elf

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/boot.o: $(SRC_DIR)/boot.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.o: $(SRC_DIR)/kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel_text.o: $(SRC_DIR)/kernel_text.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel_format.o: $(SRC_DIR)/kernel_format.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/yBash.o: $(SRC_DIR)/yBash.C | $(BUILD_DIR)
	$(CC) -x c $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/terminal_driver_legacy.o: $(SRC_DIR)/Drivers/Legacy/TerminalDriver.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtualfs.o: $(SRC_DIR)/Drivers/Latest/VirtualFS.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.elf: $(OBJS) linker.ld
	@$(KERNEL_LD) --version >/dev/null 2>&1 || { \
		echo "error: ELF linker '$(KERNEL_LD)' not found in PATH."; \
		echo "hint: install an ELF linker (for example i686-elf-ld) or place Zig at '$(ZIG)'."; \
		exit 1; \
	}
	$(KERNEL_LD) $(LDFLAGS) $(OBJS) -o $@

clean:
	rm -rf $(BUILD_DIR)
