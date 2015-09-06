ROOT := ..
include $(ROOT)/Rules.mk

TARGETS :=

ifeq ($(PLATFORM),linux)
TARGETS += scp_dump scp_write
endif

ifeq ($(PLATFORM),osx)
TARGETS += scp_dump scp_write
endif

all: $(TARGETS)

scp_dump: scp.o scp_dump.o

scp_write: scp.o scp_write.o

install: all
ifneq ($(TARGETS),)
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) scp_dump $(BINDIR)
	$(INSTALL_PROG) scp_write $(BINDIR)
endif

clean::
	$(RM) scp_dump scp_write
