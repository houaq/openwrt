include $(TOPDIR)/rules.mk

PKG_NAME:=gpio-counter
PKG_RELEASE=1

PKG_LICENSE:=GPLv2
PKG_LICENSE_FILES:=

PKG_SOURCE_URL:=https://github.com/houaq/gpio-counter
PKG_SOURCE_PROTO:=git
PKG_SOURCE_DATE:=2021-02-19
PKG_SOURCE_VERSION:=59932c59f95ee7f397a337d3e87fbfc11beaf0ce
PKG_MIRROR_HASH:=36ee44760f54ead5dbbce55d368378c36f773c567633dda54c9ca9f395feecca

PKG_MAINTAINER:=iKing Hou <houaq@qq.com>
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/package.mk

define KernelPackage/gpio-counter
  SUBMENU:=Other modules
  TITLE:=GPIO pulse counter
  DEPENDS:= +@GPIOLIB
  FILES:=$(PKG_BUILD_DIR)/gpio-counter.ko
  AUTOLOAD:=$(call AutoProbe,gpio-counter)
endef

define Build/Compile
	+$(MAKE) -C "$(LINUX_DIR)" \
		$(KERNEL_MAKE_FLAGS) \
		$(PKG_MAKE_FLAGS) \
		SUBDIRS="$(PKG_BUILD_DIR)" \
		modules
endef

define Package/kmod-gpio-counter/install
	true
endef

$(eval $(call KernelPackage,gpio-counter))
