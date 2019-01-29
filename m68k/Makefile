ROOT := ..
include $(ROOT)/Rules.mk

CFLAGS += -I.

LDFLAGS := -Lamiga -Lm68k -L../libdisk

.PHONY: m68k/m68k.a amiga/amiga.a

all: disassemble copylock m68k_emulate

copylock: m68k/m68k.a amiga/amiga.a copylock.o
	$(CC) $(LDFLAGS) $@.o -lamiga -lm68k -ldisk -o $@

disassemble: m68k/m68k.a amiga/amiga.a disassemble.o
	$(CC) $(LDFLAGS) $@.o -lamiga -lm68k -o $@

m68k_emulate: m68k/m68k.a m68k_emulate.o
	$(CC) $(LDFLAGS) $@.o -lm68k -o $@

m68k/m68k.a:
	$(MAKE) -C m68k all

amiga/amiga.a:
	$(MAKE) -C amiga all

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_PROG) copylock $(BINDIR)
	$(INSTALL_PROG) disassemble $(BINDIR)

clean::
	$(RM) disassemble copylock
	$(MAKE) -C m68k clean
	$(MAKE) -C amiga clean
