ROOT := ../..
include $(ROOT)/Rules.mk

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
PICOBJS := $(patsubst %.o,%.opic,$(OBJS))

all: containers.o containers.opic

containers.o: $(OBJS)
	$(LD) -r -o $@ $^

containers.opic: $(PICOBJS)
	$(LD) -r -o $@ $^

clean:
	$(RM) *.a *.o *.opic *.so* *~ $(DEPS)
