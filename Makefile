ROOT := .
include $(ROOT)/Rules.mk

SUBDIRS := libdisk adf disk-analyse scp

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
