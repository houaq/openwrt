#
# Copyright (C) 2013-2014 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
include $(TOPDIR)/rules.mk

ARCH:=arm
BOARD:=imx6ul
BOARDNAME:=Freescale i.MX 6 UltraLite
FEATURES:=display fpu gpio rtc usb usbgadget squashfs targz nand ubifs boot-part rootfs-part
CPU_TYPE:=cortex-a7
CPU_SUBTYPE:=neon

LINUX_VERSION:=4.1

include $(INCLUDE_DIR)/target.mk

KERNELNAME:=zImage dtbs

DEFAULT_PACKAGES += uboot-envtools mkf2fs e2fsprogs blkid

$(eval $(call BuildTarget))
