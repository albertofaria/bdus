# ---------------------------------------------------------------------------- #
# configuration

KBDUS_DEBUG  ?= 0 # must be 0 or 1
KBDUS_SPARSE ?= 0 # must be 0 or 1

KBDUS_KDIR               ?= /lib/modules/$(shell uname -r)/build
KBDUS_MODULE_INSTALL_DIR ?= /lib/modules/$(shell uname -r)/extra
KBDUS_HEADER_INSTALL_DIR ?= /usr/local/include

LIBBDUS_BINARY_INSTALL_DIR ?= /usr/local/lib
LIBBDUS_HEADER_INSTALL_DIR ?= /usr/local/include

CMDBDUS_BINARY_INSTALL_DIR          ?= /usr/local/sbin
CMDBDUS_BASH_COMPLETION_INSTALL_DIR ?= /usr/local/share/bash-completion/completions
CMDBDUS_FISH_COMPLETION_INSTALL_DIR ?= /usr/local/share/fish/vendor_completions.d
CMDBDUS_ZSH_COMPLETION_INSTALL_DIR  ?= /usr/local/share/zsh/site-functions

# ---------------------------------------------------------------------------- #

KBDUS_FLAGS := M='$(realpath kbdus)' W=1 C=$(KBDUS_SPARSE) DEBUG=$(KBDUS_DEBUG)

LIBBDUS_CFLAGS := \
-std=c99 -Wall -Wextra -Wconversion -pedantic -O2 -g -pthread \
-fvisibility=hidden -shared -fPIC -Wl,--no-undefined \
-Ikbdus/include -Ilibbdus/include -Ilibbdus/include-private $(LIBBDUS_CFLAGS)

LIBBDUS_SRCS := $(shell find libbdus/src -type f -name '*.c')

CMDBDUS_CFLAGS := \
-std=c99 -Wall -Wextra -Wconversion -pedantic -O2 -g -pthread \
-Ilibbdus/include $(CMDBDUS_CFLAGS)

# ---------------------------------------------------------------------------- #
# all

.PHONY: all
all: kbdus.ko libbdus.so bdus

# ---------------------------------------------------------------------------- #
# build

.PHONY: kbdus.ko
kbdus.ko:
	$(MAKE) -C $(KBDUS_KDIR) $(KBDUS_FLAGS)
	mv kbdus/kbdus.ko .

.PHONY: libbdus.so
libbdus.so:
	$(CC) $(LIBBDUS_CFLAGS) $(LIBBDUS_SRCS) -o $@

.PHONY: bdus
bdus: libbdus.so
	$(CC) $(CMDBDUS_CFLAGS) cmdbdus/bdus.c libbdus.so -o $@

# ---------------------------------------------------------------------------- #
# clean

.PHONY: clean
clean:
	$(MAKE) -C $(KBDUS_KDIR) $(KBDUS_FLAGS) clean
	rm -f kbdus.ko libbdus.so bdus

# ---------------------------------------------------------------------------- #
# install

.PHONY: install
install: install-kbdus install-libbdus install-cmdbdus

.PHONY: install-kbdus
install-kbdus: kbdus.ko
	# fail only if kbdus is installed and loaded but can't remove it
	! modinfo kbdus > /dev/null 2> /dev/null || modprobe -r kbdus
	install -D -m 644 -t $(KBDUS_MODULE_INSTALL_DIR) kbdus.ko
	install -D -m 644 -t $(KBDUS_HEADER_INSTALL_DIR) kbdus/include/kbdus.h
	depmod # this may take some time

.PHONY: install-libbdus
install-libbdus: libbdus.so
	install -D -m 644 -t $(LIBBDUS_BINARY_INSTALL_DIR) libbdus.so
	install -D -m 644 -t $(LIBBDUS_HEADER_INSTALL_DIR) libbdus/include/bdus.h
	ldconfig # this may take some time

.PHONY: install-cmdbdus
install-cmdbdus: bdus
	install -D -m 755 -t $(CMDBDUS_BINARY_INSTALL_DIR) bdus
	install -D -m 644 cmdbdus/completion.bash $(CMDBDUS_BASH_COMPLETION_INSTALL_DIR)/bdus
	install -D -m 644 cmdbdus/completion.fish $(CMDBDUS_FISH_COMPLETION_INSTALL_DIR)/bdus.fish
	install -D -m 644 cmdbdus/completion.zsh $(CMDBDUS_ZSH_COMPLETION_INSTALL_DIR)/_bdus

# ---------------------------------------------------------------------------- #
# uninstall

.PHONY: uninstall
uninstall: uninstall-kbdus uninstall-libbdus uninstall-cmdbdus

.PHONY: uninstall-kbdus
uninstall-kbdus:
	# fail only if kbdus is installed and loaded but can't remove it
	! modinfo kbdus > /dev/null 2> /dev/null || modprobe -r kbdus
	rm -f $(KBDUS_MODULE_INSTALL_DIR)/kbdus.ko
	rm -f $(KBDUS_HEADER_INSTALL_DIR)/kbdus.h
	depmod # this may take some time

.PHONY: uninstall-libbdus
uninstall-libbdus:
	rm -f $(LIBBDUS_BINARY_INSTALL_DIR)/libbdus.so
	rm -f $(LIBBDUS_HEADER_INSTALL_DIR)/bdus.h
	ldconfig # this may take some time

.PHONY: uninstall-cmdbdus
uninstall-cmdbdus:
	rm -f $(CMDBDUS_BINARY_INSTALL_DIR)/bdus
	rm -f $(CMDBDUS_BASH_COMPLETION_INSTALL_DIR)/bdus
	rm -f $(CMDBDUS_FISH_COMPLETION_INSTALL_DIR)/bdus.fish
	rm -f $(CMDBDUS_ZSH_COMPLETION_INSTALL_DIR)/_bdus

# ---------------------------------------------------------------------------- #
