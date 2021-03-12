#
# Copyright (C) 2006-2009 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=fibocom-gobi
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define KernelPackage/fibocom-gobi
  SUBMENU:=Other modules
  DEPENDS:= +kmod-usb-net +kmod-usb-net-qmi-wwan
  TITLE:=Driver for Fibocom F150 5G Modules
  AUTOLOAD:=$(call AutoLoad,70,GobiNet)
  FILES:=$(PKG_BUILD_DIR)/GobiNet.ko
endef

BUILDFLAGS:=-DGHT_FEATURE_MULTI_PDN

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
		$(KERNEL_MAKE_FLAGS) \
		SUBDIRS="$(PKG_BUILD_DIR)" \
		EXTRA_CFLAGS="$(BUILDFLAGS)" \
		modules
endef

$(eval $(call KernelPackage,fibocom-gobi))
