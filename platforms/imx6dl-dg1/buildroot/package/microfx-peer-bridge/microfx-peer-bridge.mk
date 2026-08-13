################################################################################
# microfx-peer-bridge
################################################################################

MICROFX_PEER_BRIDGE_VERSION = 1.0.0
MICROFX_PEER_BRIDGE_SITE = $(BR2_EXTERNAL_IMX6DL_DG1_PATH)/../../..
MICROFX_PEER_BRIDGE_SITE_METHOD = local
MICROFX_PEER_BRIDGE_SUBDIR = services/peer-bridge
MICROFX_PEER_BRIDGE_DEPENDENCIES = libpeer-microfx cjson libwebsockets
MICROFX_PEER_BRIDGE_CONF_OPTS = -DLIBPEER_INCLUDE_DIR=$(STAGING_DIR)/usr/include

$(eval $(cmake-package))
