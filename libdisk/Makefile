ROOT := ..
include $(ROOT)/Rules.mk

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
PICOBJS := $(patsubst %.o,%.opic,$(OBJS))

MAJOR_VERSION := 0
MINOR_VERSION := 0

# base name of the shared library name
SOLIB_PFX := libdisk

ifeq ($(PLATFORM),osx)
SOLIB   := $(SOLIB_PFX).dylib
SONAME  := $(SOLIB_PFX).$(MAJOR_VERSION).dylib
SOVERS  := $(SOLIB_PFX).$(MAJOR_VERSION).$(MINOR_VERSION).dylib
LDFLAGS += -dynamiclib -install_name $(SONAME)
endif

ifeq ($(PLATFORM),linux)
SOLIB   := $(SOLIB_PFX).so
SONAME  := $(SOLIB_PFX).so.$(MAJOR_VERSION)
SOVERS  := $(SOLIB_PFX).so.$(MAJOR_VERSION).$(MINOR_VERSION)
LDFLAGS += -Wl,-h,$(SONAME) -shared
endif

ifeq ($(PLATFORM),win32)
SOLIB   := $(SOLIB_PFX).dll
SONAME  := $(SOLIB_PFX).$(MAJOR_VERSION).dll
SOVERS  := $(SOLIB_PFX).$(MAJOR_VERSION).$(MINOR_VERSION).dll
LDFLAGS += -Wl,-h,$(SONAME) -shared
endif

LIBS :=
LIBS-$(caps) := -ldl
LIBS += $(LIBS-y)

all:
ifneq ($(SHARED_LIB),n)
	$(MAKE) -C stream streams.apic
	$(MAKE) -C container containers.apic
	$(MAKE) -C format formats.apic
	$(MAKE) $(SOVERS)
else
	$(MAKE) -C stream streams.o
	$(MAKE) -C container containers.o
	$(MAKE) -C format formats.o
	$(MAKE) libdisk.a
endif

libdisk.a: $(OBJS) stream/streams.o container/containers.o format/formats.o
	$(AR) rcs $@ $^

$(SOVERS): $(PICOBJS) stream/streams.apic container/containers.apic \
		format/formats.apic
	$(CC) $(LDFLAGS) -o $(SOVERS) $^ $(LIBS)
#	strip -x $(SOVERS)
	ln -sf $(SOVERS) $(SONAME)
	ln -sf $(SONAME) $(SOLIB)

install: all
ifneq ($(SHARED_LIB),n)
	$(INSTALL_DIR) $(LIBDIR)
	$(INSTALL_PROG) $(SOVERS) $(LIBDIR)
	ln -sf $(SOVERS) $(LIBDIR)/$(SONAME)
	ln -sf $(SONAME) $(LIBDIR)/$(SOLIB)
endif
	$(INSTALL_DIR) $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/disk.h $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/stream.h $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/util.h $(INCLUDEDIR)/libdisk
	$(INSTALL_DATA) include/libdisk/track_types.h $(INCLUDEDIR)/libdisk

clean::
	$(MAKE) -C stream clean
	$(MAKE) -C container clean
	$(MAKE) -C format clean
