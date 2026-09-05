BUILD_DIR := build
EXTRA_OPTIONS_DEBUG ?= 0

ifneq ($(EXTRA_OPTIONS_DEBUG),0)
ifneq ($(EXTRA_OPTIONS_DEBUG),1)
    $(error EXTRA_OPTIONS_DEBUG must be 0 or 1)
endif
endif

DEBUG_STAMP := $(BUILD_DIR)/.extra-options-debug-$(EXTRA_OPTIONS_DEBUG)

# Respect explicit toolchain selections while avoiding GNU Make's built-in
# `cc`/`ld` defaults. Apple Clang cannot target MIPS, so macOS users should
# invoke build_mod.sh, which locates Homebrew LLVM/LLD and passes them here.
ifeq ($(origin CC),default)
    CC := clang
endif
ifeq ($(origin LD),default)
    LD := ld.lld
endif

TARGET  := $(BUILD_DIR)/mod.elf

LDSCRIPT := mod.ld
CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
			-fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset \
			-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-variable \
			-Wno-missing-braces -Wno-unsupported-floating-point-opt -Werror=section
ASFLAGS  := -target mips -mips2 -mabi=32 -G0 -mno-abicalls -mno-check-zero-division -x assembler-with-cpp -modd-spreg
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -DF3DEX_GBI -I include -I include/dummy_headers \
			-I mnsg/include -I mnsg/libultra/include -I mnsg/src
override CPPFLAGS += -DEXTRA_OPTIONS_DEBUG=$(EXTRA_OPTIONS_DEBUG)
LDFLAGS  := -nostdlib -T $(LDSCRIPT) -Map $(BUILD_DIR)/mod.map --unresolved-symbols=ignore-all --emit-relocs -e 0 --no-nmagic

C_SRCS := $(shell find src -name '*.c' | sort)
C_OBJS := $(addprefix $(BUILD_DIR)/, $(C_SRCS:.c=.o))
C_DEPS := $(addprefix $(BUILD_DIR)/, $(C_SRCS:.c=.d))

S_SRCS := $(shell find src -name '*.s' | sort)
S_OBJS := $(addprefix $(BUILD_DIR)/, $(S_SRCS:.s=.o))

all: $(TARGET)

$(TARGET): $(C_OBJS) $(S_OBJS) $(LDSCRIPT) | $(BUILD_DIR)
	$(LD) $(C_OBJS) $(S_OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR):
	mkdir -p $@

# Make does not track command-line flag changes. Remove the opposite stamp
# so switching either way rebuilds objects, even without `make clean`.
$(DEBUG_STAMP): | $(BUILD_DIR)
	rm -f $(BUILD_DIR)/.extra-options-debug-0 $(BUILD_DIR)/.extra-options-debug-1
	touch $@

$(C_OBJS): $(BUILD_DIR)/%.o : %.c $(DEBUG_STAMP) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

$(S_OBJS): $(BUILD_DIR)/%.o : %.s $(DEBUG_STAMP) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(ASFLAGS) -I src -nostdinc -D_LANGUAGE_C -DMIPS -DF3DEX_GBI -I include -I include/dummy_headers -I mnsg/include -I mnsg/libultra/include -I mnsg/src $< -c -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(C_DEPS)

.PHONY: clean all
