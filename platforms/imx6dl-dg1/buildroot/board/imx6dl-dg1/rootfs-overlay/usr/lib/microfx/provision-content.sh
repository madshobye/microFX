#!/bin/sh

# Populate the volatile setup web root with captive-network probe responses.
# Every probe deliberately resolves to the management page instead of the
# operating system's expected internet-success response.
microfx_prepare_captive_content() {
  web_root=${1:?web root is required}

  mkdir -p "$web_root/library/test"

  # Older images accidentally made this probe a directory. Remove that exact
  # legacy path before creating the response symlink so upgrades repair it.
  if [ -d "$web_root/hotspot-detect.html" ] && [ ! -L "$web_root/hotspot-detect.html" ]; then
    rm -rf "$web_root/hotspot-detect.html"
  fi

  for probe in generate_204 gen_204 ncsi.txt connecttest.txt redirect canonical.html hotspot-detect.html; do
    ln -snf index.html "$web_root/$probe"
  done
  ln -snf ../../index.html "$web_root/library/test/success.html"
}
