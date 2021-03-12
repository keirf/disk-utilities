
# Default rule
all:

ARCH ?= $(shell uname -m | sed -e s/i.86/x86_32/ \
          -e s/i86pc/x86_32/ -e s/amd64/x86_64/)

ifeq ($(shell uname -s),Darwin)
PLATFORM = osx
else ifeq ($(shell uname -s | cut -c1-6),CYGWIN)
PLATFORM = win32
else ifeq ($(shell uname -s | cut -c1-5),MINGW)
PLATFORM = win32
else
PLATFORM = linux
endif

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
INCLUDEDIR = $(PREFIX)/include
LIBDIR = $(PREFIX)/lib

$(ARCH) := y
CFLAGS-$(x86_32) += -m32 -march=i686
CFLAGS-$(x86_64) += -m64

INSTALL      = install
INSTALL_DIR  = $(INSTALL) -d -m0755 -p
INSTALL_DATA = $(INSTALL) -m0644 -p
INSTALL_PROG = $(INSTALL) -m0755 -p

AR ?= ar
CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy
RM := rm -f

LDFLAGS ?=
CFLAGS ?= -O2
#CFLAGS = -O0 -g
CFLAGS += -fno-strict-aliasing -std=gnu99 -Wall
ifneq ($(PLATFORM),win32)
CFLAGS += -Werror
endif
CFLAGS += -I$(ROOT)/libdisk/include
CFLAGS += -MMD -MF $(@D)/.$(@F).d
CFLAGS += $(CFLAGS-y)

# cc-option: Check if compiler supports first option, else fall back to second.
#
# This is complicated by the fact that unrecognised -Wno-* options:
#   (a) are ignored unless the compilation emits a warning; and
#   (b) even then produce a warning rather than an error
# To handle this we do a test compile, passing the option-under-test, on a code
# fragment that will always produce a warning (integer assigned to pointer).
# We then grep for the option-under-test in the compiler's output, the presence
# of which would indicate an "unrecognized command-line option" warning/error.
#
# Usage: cflags-y += $(call cc-option,$(CC),-march=winchip-c6,-march=i586)
cc-option = $(shell if test -z "`echo 'void*p=1;' | \
              $(1) $(2) -S -o /dev/null -xc - 2>&1 | grep -- $(2)`"; \
              then echo "$(2)"; else echo "$(3)"; fi ;)

# cc-option-add: Add an option to compilation flags, but only if supported.
# Usage: $(call cc-option-add CFLAGS,CC,-march=winchip-c6)
cc-option-add = $(eval $(call cc-option-add-closure,$(1),$(2),$(3)))
define cc-option-add-closure
    ifneq ($$(call cc-option,$$($(2)),$(3),n),n)
        $(1) += $(3)
    endif
endef

cc-options-add = $(foreach o,$(3),$(call cc-option-add,$(1),$(2),$(o)))

#$(call cc-option-add,CFLAGS,CC,-Wno-unused-variable)
#$(call cc-option-add,CFLAGS,CC,-Wno-unused-but-set-variable)

DEPS += .*.d

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

PIC_CFLAGS = $(CFLAGS) -fPIC
$(call cc-option-add,PIC_CFLAGS,CC,-fvisibility=hidden)
%.opic: %.c
	$(CC) $(PIC_CFLAGS) -c -o $@ $<

.PHONY: all install clean

clean::
	$(RM) *.a *.o *apic *.opic *.so* *~ $(DEPS)

-include $(DEPS)
