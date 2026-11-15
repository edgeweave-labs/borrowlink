# Linux reference node.  Build:  make -C examples/linux-node
ROOT := ../..
CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic
INC := -I$(ROOT)/include -I$(ROOT)/adapters/common
CORE := $(ROOT)/src/wire.c $(ROOT)/src/session.c $(ROOT)/src/binding.c \
        $(ROOT)/profiles/message.c $(ROOT)/adapters/common/bl_driver.c
OUT ?= build

$(OUT)/borrowlink-linux-node: main.c linux_adapter.c cbor_min.c $(CORE)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INC) $^ $(LDFLAGS) -o $@

.PHONY: clean
clean:
	rm -rf $(OUT)
