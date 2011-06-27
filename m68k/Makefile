ROOT := .

LDFLAGS := -Lamiga -Lm68k

.PHONY: all m68k/m68k.a amiga/amiga.a

all: disassemble copylock

include $(ROOT)/Rules.mk

copylock: m68k/m68k.a amiga/amiga.a copylock.o
	$(CC) $(LDFLAGS) $@.o -lamiga -lm68k -o $@

disassemble: m68k/m68k.a amiga/amiga.a disassemble.o
	$(CC) $(LDFLAGS) $@.o -lamiga -lm68k -o $@

m68k/m68k.a:
	$(MAKE) -C m68k all

amiga/amiga.a:
	$(MAKE) -C amiga all

clean:
	$(RM) disassemble copylock *.o *~ $(DEPS)
	$(MAKE) -C m68k clean
	$(MAKE) -C amiga clean
