#ifndef MICROFX_IDENTITY_H
#define MICROFX_IDENTITY_H

// Product identity lives here so applications and platform adapters do not
// need to repeat user-facing names. Protocol names and filesystem paths use
// the stable lowercase slug.
#define MICROFX_PRODUCT_NAME "microFX"
#define MICROFX_PRODUCT_SLUG "microfx"
#define MICROFX_DEFAULT_PEER_ID MICROFX_PRODUCT_SLUG "-demo"
#define MICROFX_DEFAULT_SETUP_SSID MICROFX_PRODUCT_NAME "-setup"
#define MICROFX_DEFAULT_SETUP_PASSWORD MICROFX_PRODUCT_SLUG "setup"

#endif
