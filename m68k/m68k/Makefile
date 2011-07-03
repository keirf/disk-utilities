ROOT := ../..
include $(ROOT)/Rules.mk

all: libm68k.a

libm68k.a: m68k_emulate.o
	$(AR) rc $@ $^

clean:
	$(RM) *.a *.o *~ $(DEPS)
