include $(TOPDIR)/rules.mk

PKG_NAME:=oonf-dlep-radio-git
PKG_VERSION:=2016-04-27

CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

CMAKE_SOURCE_DIR=$(CURDIR)/../../
CMAKE_OPTIONS=-D OONF_NO_WERROR:Bool=true \
              -D OONF_LOGGING_LEVEL:String=debug \
              -D OONF_NO_TESTING:Bool=true \
              -D UCI:Bool=true \
              -D OONF_APP_DEFAULT_CFG_HANDLER:String=uci \
              -D OONF_STATIC_PLUGINS:String="class;clock;layer2;packet_socket;socket;stream_socket;telnet;timer;viewer;os_clock;os_fd;os_interface;os_system;nl80211_listener;layer2info;systeminfo;cfg_uciloader;cfg_compact;dlep_radio" \
              -D INSTALL_LIB_DIR:Path=lib/oonf \
              -D INSTALL_INCLUDE_DIR:Path=include/oonf \
              -D INSTALL_CMAKE_DIR:Path=lib/oonf \
              -D CMAKE_PREFIX_PATH=$(STAGING_DIR)/usr

define Package/oonf-git/template
	SECTION:=net
	CATEGORY:=Network
	MAINTAINER:=Henning Rogge <hrogge@gmail.com>
	SUBMENU:=OLSR.org network framework
	URL:=http://www.olsr.org/
endef

define Package/oonf-dlep-radio-git
	$(call Package/oonf-git/template) 
	TITLE:= Build DLEP Radio Agent
	DEPENDS:=+librt +libnl-tiny +libuci +oonf-init-scripts
	VERSION:=$(PKG_VERSION)
endef

Build/Compile=$(call Build/Compile/Default,dlep_radio_static)
Build/Install=

define Build/Install
	$(INSTALL_BIN) -D $(PKG_BUILD_DIR)/$(MAKE_PATH)/dlep_radio_static $(PKG_INSTALL_DIR)/usr/sbin/dlep_radio;
endef

TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include -I${STAGING_DIR}/usr/include/libnl-tiny

define Package/oonf-dlep-radio-git/install
	$(INSTALL_BIN)  -D $(PKG_BUILD_DIR)/dlep_radio_static $(1)/usr/sbin/dlep_radio
	$(INSTALL_BIN)  -D ./files/dlep_radio.init            $(1)/etc/init.d/dlep_radio
	$(INSTALL_BIN)  -D ./files/dlep_radio.hotplug         $(1)/etc/hotplug.d/iface/50-dlep_radio
	$(INSTALL_DATA) -D ./files/dlep_radio.uci             $(1)/etc/config/dlep_radio
endef

define Package/oonf-dlep-radio-git/conffiles
/etc/config/dlep_radio
endef

$(eval $(call BuildPackage,oonf-dlep-radio-git))
