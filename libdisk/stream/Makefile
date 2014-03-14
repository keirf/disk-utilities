ROOT := ../..
include $(ROOT)/Rules.mk

OBJS := stream.o kryoflux_stream.o diskread.o disk_image.o soft.o
OBJS += discferret_dfe2.o supercard_scp.o
ifeq ($(caps),y)
OBJS += caps.o
else
OBJS += caps_disabled.o
endif

PICOBJS := $(patsubst %.o,%.opic,$(OBJS))

all: streams.o streams.opic streams.a streams.apic

streams.o: $(OBJS)
	$(LD) -r -o $@ $^

streams.opic: $(PICOBJS)
	$(LD) -r -o $@ $^

streams.a: $(OBJS)
	$(AR) rcs $@ $^

streams.apic: $(PICOBJS)
	$(AR) rcs $@ $^
