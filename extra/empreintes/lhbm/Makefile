# Pilote de la dalle avec le HBM local (patch 0923), compilé seul, pour
# l'essayer sans reconstruire le paquet :
#   make KDIR=$HOME/linux-7.2.10 LOCALVERSION= KBUILD_MODPOST_WARN=1
# Source = drivers/gpu/drm/panel/panel-xiaomi-l12-36-02-0b.c avec les patchs
# 0901, 0916, 0917 et 0923 du paquet.
obj-m += panel-xiaomi-l12-36-02-0b.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
