ROOT := ..
include $(ROOT)/Rules.mk

TARGET := mfmparse

LIBS := -L../libdisk -ldisk

all:
	$(MAKE) $(TARGET)

mfmparse: mfmparse.o config.o
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) mfmparse $(BINDIR)
	$(INSTALL_DIR) $(PREFIX)/share/mfmparse
	$(INSTALL_DATA) mfmparse_formats $(PREFIX)/share/mfmparse

config.o: CFLAGS += -DPREFIX=\"$(PREFIX)\"

clean::
	$(RM) $(TARGET)
