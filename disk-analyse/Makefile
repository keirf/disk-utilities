ROOT := ..
include $(ROOT)/Rules.mk

TARGET := disk-analyse

ifeq ($(SHARED_LIB),n)
LIBS := ../libdisk/libdisk.a
else
LIBS := -L../libdisk -ldisk
endif

all:
	$(MAKE) $(TARGET)

disk-analyse: disk-analyse.o config.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) -o $@

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) disk-analyse $(BINDIR)
	$(INSTALL_DIR) $(INSTALLDIR)/share/disk-analyse
	$(INSTALL_DATA) formats $(INSTALLDIR)/share/disk-analyse

config.o: CFLAGS += -DPREFIX=\"$(PREFIX)\"

clean::
	$(RM) $(TARGET)
