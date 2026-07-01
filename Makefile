obj-m += cpu_spoof_stealth.o

KERNELDIR ?= ~/linux-5.10.252
CROSS_COMPILE ?= aarch64-linux-gnu-
ARCH ?= arm64
PWD := $(shell pwd)

all:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f *.ko *.mod.c *.o *.symvers *.order