# Use these options only to determine compiler include directories.
# ccflags-y := $(ccflags-y) -xc -E -v

# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
	obj-m := hello.o

# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -rf *.o .*.cmd *.ko *.mod *.mod.c *.order *.symvers
