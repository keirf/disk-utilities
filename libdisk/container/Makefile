ROOT := ../..
include $(ROOT)/Rules.mk

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
PICOBJS := $(patsubst %.o,%.opic,$(OBJS))

all: containers.o containers.opic containers.a containers.apic

containers.o: $(OBJS)
	$(LD) -r -o $@ $^

containers.opic: $(PICOBJS)
	$(LD) -r -o $@ $^

containers.a: $(OBJS)
	$(AR) rcs $@ $^

containers.apic: $(PICOBJS)
	$(AR) rcs $@ $^
