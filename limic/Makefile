MODULE := limic
DEVICE := limic
MODE := 444
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
FILEGRP := $(shell ls /dev/ | grep $(DEVICE))
MODGRP := $(shell lsmod | grep $(MODULE))
obj-m := $(MODULE).o

all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	rm -rf *.ko
	rm -rf *.mod.*
	rm -rf .*.cmd
	rm -rf *.o
	rm -rf *.symvers

install:
    ifneq ($(MODGRP),)
	rmmod $(MODULE)
    endif
    ifneq ($(FILEGRP),)
	rm -f /dev/$(DEVICE)
    endif
	@insmod $(MODULE).ko
	lsmod | grep limic
	@awk '$$2=="$(MODULE)" {print $$1}' /proc/devices > .major
	@mknod /dev/$(DEVICE) c `cat .major` 0
	@rm -f .major
	@chmod $(MODE) /dev/$(DEVICE)
	ls -l /dev/$(DEVICE)
	@echo LiMIC has been successfuly installed.

uninstall:
    ifneq ($(MODGRP),)
	rmmod $(MODULE)
    endif
    ifneq ($(FILEGRP),)
	rm -f /dev/$(DEVICE)
    endif
	@echo LiMIC has been successfuly uninstalled.
