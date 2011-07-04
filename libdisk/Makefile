ROOT := ..
include $(ROOT)/Rules.mk

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
PICOBJS := $(patsubst %.o,%.opic,$(OBJS))

MAJOR := 0
MINOR := 0

all:
	$(MAKE) -C stream all
	$(MAKE) libdisk.so libdisk.o libdisk_caps.o

libdisk.so: $(PICOBJS) stream/streams.opic
	$(LD) -h $(@).$(MAJOR) -shared -o $(@).$(MAJOR).$(MINOR) $^
	ln -sf $(@).$(MAJOR).$(MINOR) $(@).$(MAJOR)
	ln -sf $(@).$(MAJOR) $(@)

libdisk.o: $(OBJS) stream/streams.o
	$(LD) -r -o $@ $^
	$(OBJCOPY) --localize-hidden $@

libdisk_caps.o: $(OBJS) stream/streams_caps.o
	$(LD) -r -o $@ $^
	$(OBJCOPY) --localize-hidden $@

install: all
	$(INSTALL_DIR) $(LIBDIR)
	$(INSTALL_PROG) libdisk.so.$(MAJOR).$(MINOR) $(LIBDIR)
	ln -sf libdisk.so.$(MAJOR).$(MINOR) $(LIBDIR)/libdisk.so.$(MAJOR)
	ln -sf libdisk.so.$(MAJOR) $(LIBDIR)/libdisk.so
	$(INSTALL_DIR) $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/disk.h $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/stream.h $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/util.h $(INCLUDEDIR)/libdisk

clean:
	$(RM) *.a *.o *.opic *.so* *~ $(DEPS)
	$(MAKE) -C stream clean