#
# Copyright (C) 2009-2010 Jo-Philipp Wich <xm@subsignal.org>
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=quectel-cm
PKG_RELEASE:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/quectel-cm
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Quectel Connection Manager
  MAINTAINER:=Atsing Hou <houaq@qq.com>
  DEPENDS:=+kmod-quectel-qmi
endef

define Package/quectel-cm/description
 This package contains an utility to manage Quectel 4G module connection.
endef

define Build/Configure
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -Wall" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/quectel-cm/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/quectel-CM $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/quectel-qmi-proxy $(1)/usr/sbin/
endef

$(eval $(call BuildPackage,quectel-cm))
