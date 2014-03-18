ROOT := ..
include $(ROOT)/Rules.mk

all: scp_dump

scp_dump: scp.o scp_dump.o

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) scp_dump $(BINDIR)

clean::
	$(RM) scp_dump
