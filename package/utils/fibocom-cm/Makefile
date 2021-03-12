#
# Copyright (C) 2009-2010 Jo-Philipp Wich <xm@subsignal.org>
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=fibocom-cm
PKG_RELEASE:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/fibocom-cm
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Fibocom Connection Manager
  MAINTAINER:=Atsing Hou <houaq@qq.com>
  DEPENDS:=+kmod-fibocom-gobi
endef

define Package/fibocom-cm/description
 This package contains an utility to manage Fibocom 5G module connection.
endef

define Build/Configure
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -Wall" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		release
endef

define Package/fibocom-cm/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fibocom-dial $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/multi-pdn-manager $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fibo_qmimsg_server $(1)/usr/sbin/
endef

$(eval $(call BuildPackage,fibocom-cm))
