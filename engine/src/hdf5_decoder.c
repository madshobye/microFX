#include "microfx/hdf5_decoder.h"

#include <hdf5.h>
#include <hdf5_hl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    MICROFX_HDF5_INPUT_LIMIT = 2 * 1024 * 1024,
    MICROFX_HDF5_OUTPUT_LIMIT = 12 * 1024 * 1024,
    MICROFX_HDF5_MAX_RANK = 4,
    MICROFX_HDF5_MAX_ATTRIBUTE_VALUES = 64
};

static JSValue Hdf5Error(JSContext *ctx, const char *message)
{
    return JS_ThrowTypeError(ctx, "HDF5 decode failed: %s", message);
}

static bool ReadIndexArray(JSContext *ctx, JSValueConst value, int rank,
                           hsize_t *output, bool required)
{
    if (JS_IsNull(value) || JS_IsUndefined(value)) return !required;
    if (!JS_IsArray(ctx, value)) return false;
    JSValue lengthValue = JS_GetPropertyStr(ctx, value, "length");
    uint32_t length = 0;
    bool valid = !JS_IsException(lengthValue) &&
                 JS_ToUint32(ctx, &length, lengthValue) == 0 &&
                 length == (uint32_t)rank;
    JS_FreeValue(ctx, lengthValue);
    if (!valid) return false;
    for (int i = 0; i < rank; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, value, (uint32_t)i);
        int64_t number = -1;
        valid = !JS_IsException(item) && JS_ToInt64(ctx, &number, item) == 0 &&
                number >= 0;
        JS_FreeValue(ctx, item);
        if (!valid) return false;
        output[i] = (hsize_t)number;
    }
    return true;
}

static JSValue ShapeArray(JSContext *ctx, int rank, const hsize_t *shape)
{
    JSValue result = JS_NewArray(ctx);
    if (JS_IsException(result)) return result;
    for (int i = 0; i < rank; i++)
        JS_SetPropertyUint32(ctx, result, (uint32_t)i,
                            JS_NewInt64(ctx, (int64_t)shape[i]));
    return result;
}

typedef struct {
    JSContext *ctx;
    JSValue object;
    bool failed;
} AttributeContext;

static herr_t ReadAttribute(hid_t location, const char *name,
                            const H5A_info_t *info, void *opaque)
{
    (void)info;
    AttributeContext *context = opaque;
    hid_t attribute = H5Aopen(location, name, H5P_DEFAULT);
    hid_t type = attribute >= 0 ? H5Aget_type(attribute) : -1;
    hid_t space = type >= 0 ? H5Aget_space(attribute) : -1;
    hssize_t points = space >= 0 ? H5Sget_simple_extent_npoints(space) : -1;
    JSValue value = JS_UNDEFINED;

    if (points >= 0 && points <= MICROFX_HDF5_MAX_ATTRIBUTE_VALUES &&
        H5Tget_class(type) == H5T_STRING) {
        if (points != 1) {
            context->failed = true;
        } else if (H5Tis_variable_str(type) > 0) {
            char *text = NULL;
            if (H5Aread(attribute, type, &text) >= 0 && text) {
                value = JS_NewString(context->ctx, text);
                H5free_memory(text);
            } else context->failed = true;
        } else {
            size_t size = H5Tget_size(type);
            char *text = calloc(size + 1, 1);
            if (text && H5Aread(attribute, type, text) >= 0)
                value = JS_NewStringLen(context->ctx, text, strnlen(text, size));
            else context->failed = true;
            free(text);
        }
    } else if (points >= 0 && points <= MICROFX_HDF5_MAX_ATTRIBUTE_VALUES &&
               (H5Tget_class(type) == H5T_INTEGER ||
                H5Tget_class(type) == H5T_FLOAT)) {
        double values[MICROFX_HDF5_MAX_ATTRIBUTE_VALUES];
        if (H5Aread(attribute, H5T_NATIVE_DOUBLE, values) < 0) {
            context->failed = true;
        } else if (points == 1) {
            value = JS_NewFloat64(context->ctx, values[0]);
        } else {
            value = JS_NewArray(context->ctx);
            for (hssize_t i = 0; i < points; i++)
                JS_SetPropertyUint32(context->ctx, value, (uint32_t)i,
                                    JS_NewFloat64(context->ctx, values[i]));
        }
    }

    if (!JS_IsUndefined(value))
        JS_SetPropertyStr(context->ctx, context->object, name, value);
    if (space >= 0) H5Sclose(space);
    if (type >= 0) H5Tclose(type);
    if (attribute >= 0) H5Aclose(attribute);
    return context->failed ? -1 : 0;
}

static JSValue AttributesAt(JSContext *ctx, hid_t file, const char *path)
{
    hid_t object = H5Oopen(file, path, H5P_DEFAULT);
    if (object < 0) return JS_EXCEPTION;
    AttributeContext context = {
        .ctx = ctx,
        .object = JS_NewObject(ctx),
        .failed = false
    };
    hsize_t index = 0;
    if (JS_IsException(context.object) ||
        H5Aiterate2(object, H5_INDEX_NAME, H5_ITER_NATIVE, &index,
                    ReadAttribute, &context) < 0)
        context.failed = true;
    H5Oclose(object);
    if (context.failed) {
        JS_FreeValue(ctx, context.object);
        return JS_EXCEPTION;
    }
    return context.object;
}

static bool DatasetType(hid_t type, hid_t *nativeType, const char **name,
                        size_t *size)
{
    H5T_class_t kind = H5Tget_class(type);
    size_t bytes = H5Tget_size(type);
    if (kind == H5T_INTEGER) {
        bool sign = H5Tget_sign(type) != H5T_SGN_NONE;
        if (bytes == 1) {
            *nativeType = sign ? H5T_NATIVE_INT8 : H5T_NATIVE_UINT8;
            *name = sign ? "int8" : "uint8";
        } else if (bytes == 2) {
            *nativeType = sign ? H5T_NATIVE_INT16 : H5T_NATIVE_UINT16;
            *name = sign ? "int16" : "uint16";
        } else if (bytes == 4) {
            *nativeType = sign ? H5T_NATIVE_INT32 : H5T_NATIVE_UINT32;
            *name = sign ? "int32" : "uint32";
        } else return false;
    } else if (kind == H5T_FLOAT && bytes == 4) {
        *nativeType = H5T_NATIVE_FLOAT; *name = "float32";
    } else if (kind == H5T_FLOAT && bytes == 8) {
        *nativeType = H5T_NATIVE_DOUBLE; *name = "float64";
    } else return false;
    *size = bytes;
    return true;
}

static bool AddRequestedAttributes(JSContext *ctx, hid_t file,
                                   JSValueConst paths, JSValue result)
{
    JSValue output = JS_NewObject(ctx);
    if (JS_IsException(output)) return false;
    if (!JS_IsNull(paths) && !JS_IsUndefined(paths)) {
        if (!JS_IsArray(ctx, paths)) { JS_FreeValue(ctx, output); return false; }
        JSValue lengthValue = JS_GetPropertyStr(ctx, paths, "length");
        uint32_t length = 0;
        bool valid = !JS_IsException(lengthValue) &&
                     JS_ToUint32(ctx, &length, lengthValue) == 0 && length <= 32;
        JS_FreeValue(ctx, lengthValue);
        if (!valid) { JS_FreeValue(ctx, output); return false; }
        for (uint32_t i = 0; i < length; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, paths, i);
            const char *path = JS_ToCString(ctx, item);
            if (!path) { JS_FreeValue(ctx, item); JS_FreeValue(ctx, output); return false; }
            JSValue attributes = AttributesAt(ctx, file, path);
            if (JS_IsException(attributes)) {
                JS_FreeCString(ctx, path); JS_FreeValue(ctx, item);
                JS_FreeValue(ctx, output); return false;
            }
            JS_SetPropertyStr(ctx, output, path, attributes);
            JS_FreeCString(ctx, path); JS_FreeValue(ctx, item);
        }
    }
    JS_SetPropertyStr(ctx, result, "attributes", output);
    return true;
}

JSValue MicroFxHdf5Decode(JSContext *ctx, JSValueConst thisValue,
                          int argc, JSValueConst *argv)
{
    (void)thisValue;
    if (argc != 6) return Hdf5Error(ctx, "expected bytes, dataset, start, count, stride, and attribute paths");
    size_t inputSize = 0;
    uint8_t *input = JS_GetArrayBuffer(ctx, &inputSize, argv[0]);
    if (!input || inputSize < 8 || inputSize > MICROFX_HDF5_INPUT_LIMIT)
        return Hdf5Error(ctx, "input must be an ArrayBuffer no larger than 2 MiB");
    const char *path = JS_ToCString(ctx, argv[1]);
    if (!path || path[0] != '/') {
        if (path) JS_FreeCString(ctx, path);
        return Hdf5Error(ctx, "dataset path must be absolute");
    }

    H5Eset_auto2(H5E_DEFAULT, NULL, NULL);
    hid_t file = H5LTopen_file_image(input, inputSize,
        H5LT_FILE_IMAGE_DONT_COPY | H5LT_FILE_IMAGE_DONT_RELEASE);
    hid_t dataset = file >= 0 ? H5Dopen2(file, path, H5P_DEFAULT) : -1;
    hid_t fileSpace = dataset >= 0 ? H5Dget_space(dataset) : -1;
    hid_t fileType = dataset >= 0 ? H5Dget_type(dataset) : -1;
    int rank = fileSpace >= 0 ? H5Sget_simple_extent_ndims(fileSpace) : -1;
    hsize_t dimensions[MICROFX_HDF5_MAX_RANK] = {0};
    hsize_t start[MICROFX_HDF5_MAX_RANK] = {0};
    hsize_t count[MICROFX_HDF5_MAX_RANK] = {0};
    hsize_t stride[MICROFX_HDF5_MAX_RANK] = {1, 1, 1, 1};
    bool valid = rank > 0 && rank <= MICROFX_HDF5_MAX_RANK &&
                 H5Sget_simple_extent_dims(fileSpace, dimensions, NULL) == rank;
    if (valid && !ReadIndexArray(ctx, argv[2], rank, start, false)) valid = false;
    if (valid && !ReadIndexArray(ctx, argv[4], rank, stride, false)) valid = false;
    for (int i = 0; valid && i < rank; i++) {
        if (stride[i] == 0 || start[i] >= dimensions[i]) valid = false;
        else count[i] = (dimensions[i] - start[i] + stride[i] - 1) / stride[i];
    }
    if (valid && !JS_IsNull(argv[3]) && !JS_IsUndefined(argv[3]) &&
        !ReadIndexArray(ctx, argv[3], rank, count, true)) valid = false;
    size_t elementSize = 0; hid_t nativeType = -1; const char *typeName = NULL;
    if (valid && !DatasetType(fileType, &nativeType, &typeName, &elementSize)) valid = false;
    size_t elements = 1;
    for (int i = 0; valid && i < rank; i++) {
        if (count[i] == 0 || start[i] + (count[i] - 1) * stride[i] >= dimensions[i] ||
            count[i] > SIZE_MAX / elements) valid = false;
        else elements *= (size_t)count[i];
    }
    if (!valid || elements > MICROFX_HDF5_OUTPUT_LIMIT / elementSize) {
        if (fileType >= 0) H5Tclose(fileType);
        if (fileSpace >= 0) H5Sclose(fileSpace);
        if (dataset >= 0) H5Dclose(dataset);
        if (file >= 0) H5Fclose(file);
        JS_FreeCString(ctx, path);
        return Hdf5Error(ctx, "unsupported type, selection, rank, or output larger than 12 MiB");
    }

    hid_t memorySpace = H5Screate_simple(rank, count, NULL);
    size_t outputSize = elements * elementSize;
    void *bytes = malloc(outputSize);
    valid = memorySpace >= 0 && bytes &&
            H5Sselect_hyperslab(fileSpace, H5S_SELECT_SET, start, stride, count, NULL) >= 0 &&
            H5Dread(dataset, nativeType, memorySpace, fileSpace, H5P_DEFAULT, bytes) >= 0;
    JSValue result = JS_EXCEPTION;
    if (valid) {
        result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "format", JS_NewString(ctx, "hdf5"));
        JS_SetPropertyStr(ctx, result, "dataset", JS_NewString(ctx, path));
        JS_SetPropertyStr(ctx, result, "type", JS_NewString(ctx, typeName));
        JS_SetPropertyStr(ctx, result, "shape", ShapeArray(ctx, rank, count));
        JS_SetPropertyStr(ctx, result, "sourceShape", ShapeArray(ctx, rank, dimensions));
        JS_SetPropertyStr(ctx, result, "buffer", JS_NewArrayBufferCopy(ctx, bytes, outputSize));
        valid = !JS_IsException(result) &&
                AddRequestedAttributes(ctx, file, argv[5], result);
    }
    free(bytes);
    if (memorySpace >= 0) H5Sclose(memorySpace);
    H5Tclose(fileType); H5Sclose(fileSpace); H5Dclose(dataset); H5Fclose(file);
    JS_FreeCString(ctx, path);
    if (!valid) {
        if (!JS_IsException(result)) JS_FreeValue(ctx, result);
        return Hdf5Error(ctx, "file, dataset, or requested attributes could not be decoded");
    }
    return result;
}
