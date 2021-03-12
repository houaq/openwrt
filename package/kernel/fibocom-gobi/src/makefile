obj-m := GobiNet.o
GobiNet-objs := GobiUSBNet.o QMIDevice.o QMI.o

PWD := $(shell pwd)
OUTPUTDIR=/lib/modules/`uname -r`/kernel/drivers/net/usb/
EXTRA_CFLAGS += -DGHT_FEATURE_MULTI_PDN
ifeq ($(ARCH),)
ARCH := $(shell uname -m)
endif
ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE :=
endif
ifeq ($(KDIR),)
KDIR := /lib/modules/$(shell uname -r)/build
endif

default:
	ln -sf makefile Makefile
	$(MAKE) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -C $(KDIR) M=$(PWD) modules

install: default
	rm -rf Makefile
	rm -rf *.o *.o.* *~ core .depend .*.cmd *.mod.c .tmp_versions Module.* modules.order
	mkdir -p $(OUTPUTDIR)
	cp -f GobiNet.ko $(OUTPUTDIR)
	depmod
	modprobe -r qmi_wwan
	modprobe -r GobiNet
	modprobe GobiNet

clean:
	rm -rf Makefile
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module.* modules.order
