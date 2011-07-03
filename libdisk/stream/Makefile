ROOT := ../..
include $(ROOT)/Rules.mk

OBJS := stream.o kryoflux_stream.o diskread.o disk_image.o soft.o

OBJS += $(OBJS-y)

PICOBJS := $(patsubst %.o,%.opic,$(OBJS))

all: streams.o streams.opic streams_caps.o streams_caps.opic

streams.o: $(OBJS) caps_disabled.o
	$(LD) -r -o $@ $^
	$(OBJCOPY) --localize-hidden $@

streams.opic: $(PICOBJS) caps_disabled.opic
	$(LD) -r -o $@ $^
	$(OBJCOPY) --localize-hidden $@

streams_caps.o: $(OBJS) caps.o
	$(LD) -r -o $@ $^
	$(OBJCOPY) --localize-hidden $@

streams_caps.opic: $(PICOBJS) caps.opic
	$(LD) -r -o $@ $^
	$(OBJCOPY) --localize-hidden $@

clean:
	$(RM) *.a *.o *.opic *.so* *~ $(DEPS)
