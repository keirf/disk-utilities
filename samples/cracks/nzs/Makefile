.PHONY: all clean

all: nzs_crack.adf

nzs_crack.adf: nzs.adf code/nzs.o code/nzs_bootblock.o
	cp nzs.adf $@
	adfbb $@ -gcode/nzs_bootblock.o
	adfwrite $@ code/nzs.o 2 10 -f -e46414d45

clean:
	$(RM) -f nzs_crack.adf
