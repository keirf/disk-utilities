ROOT := ..
include $(ROOT)/Rules.mk

OBJS := amiga.o logging.o disk.o cia.o event.o custom.o amiga_reg_names.o
OBJS += mem.o exec.o

.PHONY: all

all: libamiga.a

libamiga.a: $(OBJS)
	$(AR) rc $@ $^

clean:
	$(RM) *.a *.o *~ $(DEPS)
