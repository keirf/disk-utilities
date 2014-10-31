.PHONY: all clean

all:
	$(MAKE) -C gnu
	$(MAKE) nzs_crack.adf

nzs_crack.adf: nzs.adf gnu/nzs.bin gnu/nzs_bootblock.bin
	cp nzs.adf $@
	adfbb $@ -rgnu/nzs_bootblock.bin
	adfwrite $@ gnu/nzs.bin 2 10 -r -f -e46414d45

clean:
	$(MAKE) -C gnu clean
	$(RM) nzs_crack.adf
