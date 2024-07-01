# Requires Dark Side IPF as darkside.ipf in this directory
# Then "make" to produce cracked 0.drk and darkside_cracked.adf
#
# Other prerequisites:
#  Python 3
#  python3 -m pip install amitools
#  Installed disk-analyse ("caps=y make install")
#  cd m68k && make

ROOT := ../../..
include $(ROOT)/Rules.mk

.PHONY: all darkside clean

all: darkside

darkside:
	rm -rf tmp && mkdir tmp
	disk-analyse darkside.ipf tmp/darkside.adf --format=amigados_copylock
	disk-analyse darkside.ipf tmp/darkside.dsk --format=amigados_copylock
	xdftool tmp/darkside.adf unpack tmp
	cp tmp/DARK\ SIDE/0.drk tmp/oldexe
	$(ROOT)/m68k/copylock tmp/darkside.dsk --load=tmp/oldexe:20000 --dump=tmp/dump:3e6
	python3 ./mk_cracked_exe.py tmp/dump tmp/newexe
	cp tmp/newexe tmp/DARK\ SIDE/0.drk
	xdftool tmp/darkside_cracked.adf pack tmp/DARK\ SIDE
	md5sum -c darkside.md5

clean::
	rm -rf tmp
