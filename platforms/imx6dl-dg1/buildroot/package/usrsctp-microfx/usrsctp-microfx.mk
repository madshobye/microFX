################################################################################
# usrsctp-microfx
################################################################################

USRSCTP_MICROFX_VERSION = 01cc4e042e2235b29d9d489d89728a6f9ac063ed
USRSCTP_MICROFX_SITE = $(call github,sctplab,usrsctp,$(USRSCTP_MICROFX_VERSION))
USRSCTP_MICROFX_LICENSE = BSD-3-Clause
USRSCTP_MICROFX_LICENSE_FILES = LICENSE.md
USRSCTP_MICROFX_INSTALL_STAGING = YES
USRSCTP_MICROFX_CONF_OPTS = \
	-Dsctp_build_programs=OFF \
	-Dsctp_build_shared_lib=ON \
	-Dsctp_debug=OFF \
	-Dsctp_werror=OFF \
	-Dsctp_inet6=OFF

$(eval $(cmake-package))
