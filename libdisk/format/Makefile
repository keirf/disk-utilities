ROOT := ../..
DEPS := amiga/.*.d
include $(ROOT)/Rules.mk

SRCS := $(wildcard *.c amiga/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
PICOBJS := $(patsubst %.o,%.opic,$(OBJS))

all: formats.o formats.opic formats.a formats.apic

formats.o: $(OBJS)
	$(LD) -r -o $@ $^

formats.opic: $(PICOBJS)
	$(LD) -r -o $@ $^

formats.a: $(OBJS)
	$(AR) rcs $@ $^

formats.apic: $(PICOBJS)
	$(AR) rcs $@ $^

clean::
	$(MAKE) -C amiga clean
