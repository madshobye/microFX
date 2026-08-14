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
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/microfx-provision-control \
		$(@D)/services/provision/src/control.c
endef

define MICROFX_PROVISION_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/microfx-provision-save \
		$(TARGET_DIR)/www/cgi-bin/save
	$(INSTALL) -D -m 0755 $(@D)/microfx-provision-control \
		$(TARGET_DIR)/www/cgi-bin/control
	$(INSTALL) -D -m 0644 $(@D)/services/provision/www/index.html \
		$(TARGET_DIR)/www/index.html
	$(INSTALL) -D -m 0644 $(@D)/services/provision/www/portal-app.js \
		$(TARGET_DIR)/www/portal-app.js
	$(INSTALL) -D -m 0755 $(@D)/services/provision/www/cgi-bin/index.cgi \
		$(TARGET_DIR)/www/cgi-bin/index.cgi
	$(INSTALL) -d $(TARGET_DIR)/www/studio/vendor
	for file in index.html style.css app.js actions.js interaction-check.js protocol.js \
		reconnect-session.js studio-state.js favicon.svg; do \
		$(INSTALL) -m 0644 $(@D)/web/editor/$$file $(TARGET_DIR)/www/studio/$$file; \
	done
	for file in peerjs-1.5.5.min.js ace-1.39.1.js mode-javascript.js \
		theme-tomorrow_night_eighties.js worker-javascript.js LICENSE.peerjs \
		LICENSE.ace SHA256SUMS; do \
		$(INSTALL) -m 0644 $(@D)/web/editor/vendor/$$file $(TARGET_DIR)/www/studio/vendor/$$file; \
	done
endef

$(eval $(generic-package))
