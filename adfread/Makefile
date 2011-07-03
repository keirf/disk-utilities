ROOT := ..
include $(ROOT)/Rules.mk

all: adfread

adfread: adfread.o

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) adfread $(BINDIR)

clean:
	$(RM) adfread *.o *~
