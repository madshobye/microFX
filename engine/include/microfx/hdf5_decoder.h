#ifndef MICROFX_HDF5_DECODER_H
#define MICROFX_HDF5_DECODER_H

#include <quickjs/quickjs.h>

JSValue MicroFxHdf5Decode(JSContext *ctx, JSValueConst thisValue,
                          int argc, JSValueConst *argv);

#endif
