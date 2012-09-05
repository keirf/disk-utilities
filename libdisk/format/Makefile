ROOT := ../..
include $(ROOT)/Rules.mk

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
PICOBJS := $(patsubst %.o,%.opic,$(OBJS))

all: formats.o formats.opic

formats.o: $(OBJS)
	$(LD) -r -o $@ $^

formats.a: $(PICOBJS)
	$(AR) rs $@ $^

clean:
	$(RM) *.a *.o *.opic *.so* *~ $(DEPS)
