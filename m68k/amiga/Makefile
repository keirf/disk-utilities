ROOT := ../..
include $(ROOT)/Rules.mk

CFLAGS += -I..

OBJS := amiga.o logging.o disk.o cia.o event.o custom.o amiga_reg_names.o
OBJS += mem.o exec.o

all: libamiga.a

libamiga.a: $(OBJS)
	$(AR) rcs $@ $^
