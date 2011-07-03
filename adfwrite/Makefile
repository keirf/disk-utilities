ROOT := ..
include $(ROOT)/Rules.mk

all: adfwrite

adfwrite: adfwrite.o

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) adfwrite $(BINDIR)

clean:
	$(RM) adfwrite *.o *~
