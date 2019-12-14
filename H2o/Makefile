ccflags-y := -Wall

obj-m := h2o.o
h2o-objs := kmutex.o h2o-impl.o

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

#include $(KDIR)/.config

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

syncread-impl.o kmutex.o: kmutex.h

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
