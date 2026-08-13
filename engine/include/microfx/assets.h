#ifndef MICROFX_ASSETS_H
#define MICROFX_ASSETS_H

#include <stdbool.h>
#include <stddef.h>

bool MicroFxProjectRoot(const char *scriptPath, char *output, size_t outputSize,
                       char *error, size_t errorSize);
bool MicroFxResolveAsset(const char *projectRoot, const char *assetPath,
                        char *output, size_t outputSize,
                        char *error, size_t errorSize);

#endif
