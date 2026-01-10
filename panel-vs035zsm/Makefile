# Build the VS035ZSM panel kernel module

obj-m += panel-vs035zsm.o
panel-vs035zsm-y := drivers/panel-vs035zsm.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

