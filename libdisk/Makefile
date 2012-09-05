ROOT := ..
include $(ROOT)/Rules.mk

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
PICOBJS := $(patsubst %.o,%.opic,$(OBJS))

MAJOR := 0
MINOR := 0

LIBS :=
LIBS-$(caps) := -ldl
LIBS += $(LIBS-y)

all:
ifneq ($(SHARED_LIB),n)
	$(MAKE) -C stream streams.opic
	$(MAKE) -C container containers.opic
	$(MAKE) -C format formats.opic
	$(MAKE) libdisk.so
else
	$(MAKE) -C stream streams.o
	$(MAKE) -C container containers.o
	$(MAKE) -C format formats.o
	$(MAKE) libdisk.a
endif

libdisk.a: $(OBJS) stream/streams.o container/containers.o format/formats.o
	$(AR) rc $@ $^

libdisk.so: $(PICOBJS) stream/streams.opic container/containers.opic \
		format/formats.opic
	$(CC) -Wl,-h,$(@).$(MAJOR) -shared $(LIBS) -o $(@).$(MAJOR).$(MINOR) $^
	ln -sf $(@).$(MAJOR).$(MINOR) $(@).$(MAJOR)
	ln -sf $(@).$(MAJOR) $(@)

install: all
	$(INSTALL_DIR) $(LIBDIR)
	$(INSTALL_PROG) libdisk.so.$(MAJOR).$(MINOR) $(LIBDIR)
	ln -sf libdisk.so.$(MAJOR).$(MINOR) $(LIBDIR)/libdisk.so.$(MAJOR)
	ln -sf libdisk.so.$(MAJOR) $(LIBDIR)/libdisk.so
	$(INSTALL_DIR) $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/disk.h $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/stream.h $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/util.h $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/track_types.h $(INCLUDEDIR)/libdisk

clean:
	$(RM) *.a *.o *.opic *.so* *~ $(DEPS)
	$(MAKE) -C stream clean
	$(MAKE) -C container clean
	$(MAKE) -C format clean
