################################################################################
# microfx-provision
################################################################################

MICROFX_PROVISION_VERSION = 1.0.0
MICROFX_PROVISION_SITE = $(BR2_EXTERNAL_IMX6DL_DG1_PATH)/../../..
MICROFX_PROVISION_SITE_METHOD = local

define MICROFX_PROVISION_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/microfx-provision-save \
		$(@D)/services/provision/src/save.c
endef

define MICROFX_PROVISION_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/microfx-provision-save \
		$(TARGET_DIR)/www/cgi-bin/save
	$(INSTALL) -D -m 0644 $(@D)/services/provision/www/index.html \
		$(TARGET_DIR)/www/index.html
endef

$(eval $(generic-package))
