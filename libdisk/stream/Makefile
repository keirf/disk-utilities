ROOT := ../..
include $(ROOT)/Rules.mk

OBJS := stream.o kryoflux_stream.o diskread.o disk_image.o soft.o
ifeq ($(caps),y)
OBJS += caps.o
else
OBJS += caps_disabled.o
endif

PICOBJS := $(patsubst %.o,%.opic,$(OBJS))

all: streams.o streams.opic

streams.o: $(OBJS)
	$(LD) -r -o $@ $^

streams.opic: $(PICOBJS)
	$(LD) -r -o $@ $^

clean:
	$(RM) *.a *.o *.opic *.so* *~ $(DEPS)
