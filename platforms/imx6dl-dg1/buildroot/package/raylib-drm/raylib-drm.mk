################################################################################
# raylib-drm
################################################################################

RAYLIB_DRM_VERSION = 5.5
RAYLIB_DRM_SITE = $(call github,raysan5,raylib,$(RAYLIB_DRM_VERSION))
RAYLIB_DRM_LICENSE = Zlib
RAYLIB_DRM_LICENSE_FILES = LICENSE
RAYLIB_DRM_INSTALL_STAGING = YES
RAYLIB_DRM_DEPENDENCIES = libdrm mesa3d

define RAYLIB_DRM_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/src \
		CC="$(TARGET_CC)" AR="$(TARGET_AR)" \
		INCLUDE_PATHS="-I. -I$(STAGING_DIR)/usr/include/libdrm" \
		PLATFORM=PLATFORM_DRM GRAPHICS=GRAPHICS_API_OPENGL_ES2 \
		RAYLIB_LIBTYPE=SHARED
endef

define RAYLIB_DRM_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/src/raylib.h $(STAGING_DIR)/usr/include/raylib.h
	$(INSTALL) -D -m 0644 $(@D)/src/rlgl.h $(STAGING_DIR)/usr/include/rlgl.h
	$(INSTALL) -D -m 0644 $(@D)/src/raymath.h $(STAGING_DIR)/usr/include/raymath.h
	$(INSTALL) -D -m 0755 $(@D)/src/libraylib.so.5.5.0 \
		$(STAGING_DIR)/usr/lib/libraylib.so.5.5.0
	ln -sf libraylib.so.5.5.0 $(STAGING_DIR)/usr/lib/libraylib.so.550
	ln -sf libraylib.so.550 $(STAGING_DIR)/usr/lib/libraylib.so
endef

define RAYLIB_DRM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/src/libraylib.so.5.5.0 \
		$(TARGET_DIR)/usr/lib/libraylib.so.5.5.0
	ln -sf libraylib.so.5.5.0 $(TARGET_DIR)/usr/lib/libraylib.so.550
	ln -sf libraylib.so.550 $(TARGET_DIR)/usr/lib/libraylib.so
endef

$(eval $(generic-package))
