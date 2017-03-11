OBJS += nzs_crack.o
OBJS += trainer.o
OBJS += util.o

GZIP = /home/keir/devel/gzip-1.6/gzip
#GZIP = gzip

include Rules.mk

.PHONY: all
all: nzs_bootblock.bin nzs.bin
	$(MAKE) nzs_crack.adf

nzs_crack.adf: nzs.adf nzs.bin nzs_bootblock.bin
	cp nzs.adf $@
	cp nzs_bootblock.bin combined.bin
	$(RM) *.gz
	cat a >>nzs.bin
	$(GZIP) -9 nzs.bin
	./degzip -H nzs.bin.gz nzs_compressed.bin
	cat nzs_compressed.bin >>combined.bin
	adfwrite $@ combined.bin 0 754 -r -f -c
