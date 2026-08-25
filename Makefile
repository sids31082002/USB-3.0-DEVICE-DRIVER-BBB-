obj-m += usb_mass_storage.o

usb_mass_storage-objs := driver/usb_mass_storage.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	sudo insmod usb_mass_storage.ko

uninstall:
	sudo rmmod usb_mass_storage

logs:
	dmesg | tail -n 100
