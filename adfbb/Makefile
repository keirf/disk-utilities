ROOT := ..
include $(ROOT)/Rules.mk

all: adfbb

adfbb: adfbb.o

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) adfbb $(BINDIR)

clean:
	$(RM) adfbb *.o *~
