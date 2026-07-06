CC ?= clang
KERNEL_LD ?= i686-elf-ld
ZIG ?= tools/zig/zig

ifeq ($(shell command -v $(KERNEL_LD) >/dev/null 2>&1; echo $$?),1)
ifneq ($(wildcard $(ZIG)),)
KERNEL_LD := $(ZIG) ld.lld
endif
endif

CFLAGS := -target i686-elf -ffreestanding -fno-stack-protector -fno-pic -m32 -O2 -Wall -Wextra
ASFLAGS := -target i686-elf -m32 -ffreestanding
LDFLAGS := -m elf_i386 -T linker.ld --build-id=none

BUILD_DIR := build
SRC_DIR := src

OBJS := $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o

.PHONY: all clean

all: $(BUILD_DIR)/kernel.elf

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/boot.o: $(SRC_DIR)/boot.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.o: $(SRC_DIR)/kernel.c | $(BUILD_DIR)
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
