ROOT := ..
include $(ROOT)/Rules.mk

all: ipfinfo

ipfinfo: ipfinfo.o

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) ipfinfo $(BINDIR)

clean:
	$(RM) ipfinfo *.o *~
