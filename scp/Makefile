ROOT := ..
include $(ROOT)/Rules.mk

TARGETS :=

ifeq ($(PLATFORM),linux)
TARGETS += scp_dump
endif

all: $(TARGETS)

scp_dump: scp.o scp_dump.o

install: all
ifeq ($(PLATFORM),linux)
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) scp_dump $(BINDIR)
endif

clean::
	$(RM) scp_dump
