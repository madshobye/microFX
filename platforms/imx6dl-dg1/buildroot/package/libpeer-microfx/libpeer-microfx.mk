################################################################################
# libpeer-microfx
################################################################################

LIBPEER_MICROFX_VERSION = 9319aa434cb9e893faed0293ba9d2a21eca59c8b
LIBPEER_MICROFX_SITE = $(call github,sepfy,libpeer,$(LIBPEER_MICROFX_VERSION))
LIBPEER_MICROFX_LICENSE = MIT
LIBPEER_MICROFX_LICENSE_FILES = LICENSE
LIBPEER_MICROFX_INSTALL_STAGING = YES
LIBPEER_MICROFX_DEPENDENCIES = mbedtls libsrtp usrsctp-microfx
LIBPEER_MICROFX_CONF_OPTS = \
	-DBUILD_SHARED_LIBS=ON \
	-DBUILD_LIBPEER_EXAMPLES=OFF \
	-DLIBPEER_USE_SYSTEM_DEPS=ON \
	-DCONFIG_USE_USRSCTP=1

$(eval $(cmake-package))
