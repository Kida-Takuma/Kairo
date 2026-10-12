BOARD   ?= host
BUILD   ?= build
CC      ?= cc

QJS_DIR  := third_party/quickjs-ng
QJS_SRCS := quickjs.c libregexp.c libunicode.c dtoa.c
QJS_OBJS := $(QJS_SRCS:%.c=$(BUILD)/qjs/%.o)

NATIVE_SRCS ?=
NATIVE_INC ?=
SRCS := src/loop.c src/rt.c src/sys.c src/sys_net.c src/sys_periph.c src/bridge.c src/crc32.c src/app_image.c src/upload.c src/main_device.c $(wildcard hal/$(BOARD)/*.c)
ifeq ($(BOARD),host)
SRCS += src/main_host.c hal/common/posix_net.c
TARGET := $(BUILD)/$(BOARD)/kairo-sim
else
TARGET := $(BUILD)/$(BOARD)/kairo.elf
endif
OBJS := $(SRCS:%.c=$(BUILD)/$(BOARD)/%.o) $(foreach f,$(NATIVE_SRCS),$(BUILD)/$(BOARD)/native/$(notdir $(f:.c=.o)))

WARN   := -Wall -Wextra -Wshadow -Wstrict-prototypes
CFLAGS := -std=gnu11 -Os -g $(WARN) -Iinclude -I$(QJS_DIR) $(addprefix -I,$(NATIVE_INC))
QJS_CFLAGS := -std=gnu11 -Os -g -w -DQUICKJS_NG_BUILD -D_GNU_SOURCE
LDFLAGS := -lm

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(OBJS) $(QJS_OBJS)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILD)/$(BOARD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

define NATIVE_RULE
$(BUILD)/$(BOARD)/native/$(notdir $(1:.c=.o)): $(1)
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) -c -o $$@ $$<
endef
$(foreach f,$(NATIVE_SRCS),$(eval $(call NATIVE_RULE,$(f))))

$(BUILD)/qjs/%.o: $(QJS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(QJS_CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)
