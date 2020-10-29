#
# Copyright (C) 2012-2014 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

FAT32_BLOCK_SIZE=1024
FAT32_BLOCKS=$(shell echo $$(($(CONFIG_IMX6UL_SD_BOOT_PARTSIZE)*1024*1024/$(FAT32_BLOCK_SIZE))))

UBIFS_OPTS = -F -m 2048 -e 124KiB -c 4096 -U
UBI_OPTS = -m 2048 -p 128KiB -s 512 -O 2048

define Build/imx-sdcard
	rm -f $@.boot
	mkfs.fat $@.boot -C $(FAT32_BLOCKS)

	mcopy -i $@.boot u-boot.imx ::u-boot.imx
	mcopy -i $@.boot $(DTS_DIR)/$(DEVICE_DTS).dtb ::$(DEVICE_DTS).dtb
	mcopy -i $@.boot $(IMAGE_KERNEL) ::/zImage
	./gen_sdcard_img.sh $@ \
		$@.boot \
		$(IMAGE_ROOTFS) \
		$(CONFIG_IMX6UL_SD_BOOT_PARTSIZE) \
		$(CONFIG_TARGET_ROOTFS_PARTSIZE)
	rm -f $@.boot
endef

define Device/Default
  PROFILES := Default
  DEVICE_VARS :=
  KERNEL_NAME := zImage
  KERNEL := kernel-bin
  IMAGES := 
  IMAGE/sdcard.img.gz := imx-sdcard | append-metadata | gzip
  SUPPORTED_DEVICES = hd6ul hd6ul-emmc
endef

define Device/hd6ul
  DEVICE_TITLE := Vanxoak HD6UL
  DEVICE_DTS := hd-imx6ull-core-256m
endef

TARGET_DEVICES += hd6ul

define Device/hd6ul-emmc
  DEVICE_TITLE := Vanxoak HD6UL (eMMC)
  DEVICE_DTS := hd-imx6ull-core-emmc
endef

TARGET_DEVICES += hd6ul-emmc

$(eval $(call BuildImage))
