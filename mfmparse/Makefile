ROOT := ..
include $(ROOT)/Rules.mk

TARGET := mfmparse

ifeq ($(caps),y)
LIBS := -ldl -lstdc++
LIBDISK_OBJ := ../libdisk/libdisk_caps.o
else
LIBS :=
LIBDISK_OBJ := ../libdisk/libdisk.o
endif

all:
	$(MAKE) $(TARGET)

mfmparse: mfmparse.o $(LIBDISK_OBJ) config.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) mfmparse $(BINDIR)
	$(INSTALL_DIR) $(PREFIX)/share/mfmparse
	$(INSTALL_DATA) mfmparse_formats $(PREFIX)/share/mfmparse

config.o: CFLAGS += -DPREFIX=\"$(PREFIX)\"

clean:
	$(RM) $(TARGET) *.a *.o *.opic *~ $(DEPS)
