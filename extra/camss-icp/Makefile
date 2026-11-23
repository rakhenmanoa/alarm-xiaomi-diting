# Compile le seul module qcom-camss-icp contre les en-têtes du noyau en cours.
# Usage : make extraire && make
KDIR ?= /usr/lib/modules/$(shell uname -r)/build
PATCH ?= $(CURDIR)/../../pkgbuilds/linux-diting/0934-media-qcom-camss-Add-the-ICP-driver-and-its-HFI-layer.patch

obj-m += qcom-camss-icp.o
qcom-camss-icp-objs := camss-icp.o camss-icp-hfi.o camss-bps.o

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

# Les fichiers neufs du patch 0934 ; les hunks sur camss.c, Kconfig et
# Makefile échouent ici (fichiers absents), c'est normal.
extraire:
	rm -rf .tmp && mkdir .tmp
	-patch -Np1 -d .tmp --no-backup-if-mismatch < $(PATCH) >/dev/null
	cp .tmp/drivers/media/platform/qcom/camss/camss-icp*.[ch] .tmp/drivers/media/platform/qcom/camss/camss-bps.c .
	rm -rf .tmp

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
