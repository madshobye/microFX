################################################################################
# microfx-demo
################################################################################

MICROFX_DEMO_VERSION = 1.0.0
MICROFX_DEMO_SITE = $(BR2_EXTERNAL_IMX6DL_DG1_PATH)/../../..
MICROFX_DEMO_SITE_METHOD = local
MICROFX_DEMO_DEPENDENCIES = raylib-drm quickjs libqrencode libcurl libwebsockets hdf5-microfx

define MICROFX_DEMO_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -I$(@D)/engine/include \
		-o $(@D)/canvas-demo $(@D)/engine/runtime/main.c \
		$(@D)/engine/src/scene.c $(@D)/engine/src/sdf_renderer.c \
		$(@D)/engine/src/quality.c \
		$(@D)/engine/src/quad_renderer.c \
		$(@D)/engine/src/mesh_renderer.c $(@D)/engine/src/text_renderer.c \
		$(@D)/engine/src/image_renderer.c \
		$(@D)/engine/src/outline_renderer.c \
		$(@D)/engine/src/tile_renderer.c \
		$(@D)/engine/src/gpu_texture_renderer.c \
		$(@D)/engine/src/script.c $(@D)/engine/src/assets.c \
		$(@D)/engine/src/hdf5_decoder.c \
		$(@D)/engine/src/network.c \
		$(TARGET_LDFLAGS) -Wl,-rpath,'$$ORIGIN' \
		-lraylib -lEGL -lGLESv2 -ldrm -lgbm \
		-L$(STAGING_DIR)/usr/lib/quickjs -lquickjs -lqrencode -lcurl -lwebsockets \
		-lhdf5_hl -lhdf5 -lz -lm -lpthread -ldl -latomic
endef

define MICROFX_DEMO_INSTALL_TARGET_CMDS
	# Buildroot keeps the target tree between package rebuilds. Clear files
	# previously owned by this package before installing the current set.
	rm -f $(TARGET_DIR)/usr/share/canvas/*.fs $(TARGET_DIR)/usr/share/canvas/*.vs \
		$(TARGET_DIR)/usr/share/canvas/head.obj \
		$(TARGET_DIR)/usr/share/canvas/head-low.obj \
		$(TARGET_DIR)/usr/share/canvas/icosahedron.obj
	$(INSTALL) -D -m 0755 $(@D)/canvas-demo $(TARGET_DIR)/usr/bin/canvas-demo
	$(INSTALL) -D -m 0644 $(@D)/apps/demo/assets/models/icosahedron.obj $(TARGET_DIR)/usr/share/microfx/icosahedron.obj
	$(INSTALL) -D -m 0644 $(@D)/apps/demo/assets/shaders/Light.vs $(TARGET_DIR)/usr/share/canvas/Light.vs
	$(INSTALL) -D -m 0644 $(@D)/apps/demo/assets/shaders/Light.fs $(TARGET_DIR)/usr/share/canvas/Light.fs
	$(INSTALL) -D -m 0644 $(@D)/apps/demo/scripts/main.js $(TARGET_DIR)/usr/share/microfx/main.js
	$(INSTALL) -D -m 0644 $(@D)/apps/demo/project.json $(TARGET_DIR)/usr/share/microfx/projects/demo/project.json
	$(INSTALL) -D -m 0644 $(@D)/apps/demo/scripts/main.js $(TARGET_DIR)/usr/share/microfx/projects/demo/main.js
	$(INSTALL) -D -m 0644 $(@D)/apps/demo/assets/models/icosahedron.obj $(TARGET_DIR)/usr/share/microfx/projects/demo/assets/models/icosahedron.obj
	$(INSTALL) -D -m 0644 $(@D)/apps/demo/assets/shaders/Light.vs $(TARGET_DIR)/usr/share/microfx/projects/demo/assets/shaders/Light.vs
	$(INSTALL) -D -m 0644 $(@D)/apps/demo/assets/shaders/Light.fs $(TARGET_DIR)/usr/share/microfx/projects/demo/assets/shaders/Light.fs
	cp -R $(@D)/apps/projects/. $(TARGET_DIR)/usr/share/microfx/projects/
	$(INSTALL) -D -m 0644 $(@D)/apps/onboarding/scripts/main.js $(TARGET_DIR)/usr/share/microfx/onboarding.js
	$(INSTALL) -D -m 0644 $(@D)/apps/error/scripts/main.js $(TARGET_DIR)/usr/share/microfx/error.js
endef

$(eval $(generic-package))
