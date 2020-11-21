#
# Copyright (C) 2006-2009 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=quectel-qmi
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define KernelPackage/quectel-qmi
  SUBMENU:=Other modules
  DEPENDS:= +kmod-usb-net +kmod-usb-wdm
  TITLE:=Driver for Quectel EC2x 4G LTE Modules
  AUTOLOAD:=$(call AutoLoad,70,qmi_wwan_q)
  FILES:=$(PKG_BUILD_DIR)/qmi_wwan_q.ko
endef

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
		$(KERNEL_MAKE_FLAGS) \
		SUBDIRS="$(PKG_BUILD_DIR)" \
		EXTRA_CFLAGS="$(BUILDFLAGS)" \
		modules
endef

$(eval $(call KernelPackage,quectel-qmi))
