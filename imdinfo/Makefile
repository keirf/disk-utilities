ROOT := ..
include $(ROOT)/Rules.mk

all: imdinfo

imdinfo: imdinfo.o

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) imdinfo $(BINDIR)

clean::
	$(RM) imdinfo
