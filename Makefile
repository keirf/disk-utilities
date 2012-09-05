ROOT := .
include $(ROOT)/Rules.mk

SUBDIRS := libdisk adfbb adfread adfwrite m68k mfmparse

all:
	@set -e; for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir all; \
	done

install:
	@set -e; for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir install; \
	done

clean::
	@set -e; for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir clean; \
	done
