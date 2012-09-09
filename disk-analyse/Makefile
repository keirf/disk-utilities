ROOT := ..
include $(ROOT)/Rules.mk

TARGET := disk-analyse

LIBS := -L../libdisk -ldisk

all:
	$(MAKE) $(TARGET)

disk-analyse: disk-analyse.o config.o
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) disk-analyse $(BINDIR)
	$(INSTALL_DIR) $(PREFIX)/share/disk-analyse
	$(INSTALL_DATA) formats $(PREFIX)/share/disk-analyse

config.o: CFLAGS += -DPREFIX=\"$(PREFIX)\"

clean::
	$(RM) $(TARGET)
