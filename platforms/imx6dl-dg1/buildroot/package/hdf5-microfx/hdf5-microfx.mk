################################################################################
# hdf5-microfx
################################################################################

HDF5_MICROFX_VERSION = 1.14.6
HDF5_MICROFX_SOURCE = hdf5_$(HDF5_MICROFX_VERSION).tar.gz
HDF5_MICROFX_SITE = https://github.com/HDFGroup/hdf5/archive/refs/tags
HDF5_MICROFX_INSTALL_STAGING = YES
HDF5_MICROFX_DEPENDENCIES = zlib
HDF5_MICROFX_LICENSE = BSD-3-Clause
HDF5_MICROFX_LICENSE_FILES = COPYING

HDF5_MICROFX_CONF_OPTS = \
	--disable-tools \
	--disable-tests \
	--disable-cxx \
	--disable-fortran \
	--disable-parallel \
	--enable-hl \
	--with-zlib=$(STAGING_DIR)/usr

$(eval $(autotools-package))
