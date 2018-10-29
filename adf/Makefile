ROOT := ..
include $(ROOT)/Rules.mk

TARGETS := adfbb adfread adfwrite

all: $(TARGETS)

%: %.o

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) $(TARGETS) $(BINDIR)

clean::
	$(RM) $(TARGETS)
