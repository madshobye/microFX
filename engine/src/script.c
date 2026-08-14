#include "microfx/script.h"
#include "microfx/assets.h"
#include "microfx/identity.h"
#include "microfx/network.h"
#include <quickjs/quickjs.h>
#include <qrencode.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MicroFxScript {
    JSRuntime *runtime;
    JSContext *context;
    JSValue update;
    JSValue beginFrame;
    JSValue endFrame;
    MicroFxNetwork *network;
    MicroFxScene *scene;
    char projectRoot[MICROFX_MAX_ASSET_PATH];
};

static void DumpException(JSContext *ctx);

// Generated from engine/runtime/retained.js by engine/tools/embed-runtime.py.
#include "runtime_js.inc"

static uint32_t ColorArg(JSContext *ctx, JSValueConst value)
{
    uint32_t color = 0xffffffffu;
    JS_ToUint32(ctx, &color, value);
    return color;
}

static JSValue Handle(JSContext *ctx, int handle)
{
    if (handle < 0) return JS_ThrowRangeError(ctx,"retained scene capacity exceeded");
    return JS_NewInt32(ctx,handle);
}

static JSValue AddSdfCircle(JSContext *ctx, JSValueConst thisValue,
                         int argc, JSValueConst *argv)
{
    (void)thisValue;
    MicroFxScript *script = JS_GetContextOpaque(ctx);
    double x=0,y=0,r=0;
    if (argc < 4 || JS_ToFloat64(ctx,&x,argv[0]) || JS_ToFloat64(ctx,&y,argv[1]) ||
        JS_ToFloat64(ctx,&r,argv[2])) return JS_ThrowTypeError(ctx,"sdfCircle(x,y,r,rgba)");
    return Handle(ctx,MicroFxSceneAddCircle(script->scene,x,y,r,ColorArg(ctx,argv[3])));
}

static JSValue AddFastCircle(JSContext *ctx, JSValueConst thisValue,
                             int argc, JSValueConst *argv)
{
    (void)thisValue;
    MicroFxScript *script = JS_GetContextOpaque(ctx);
    double x=0,y=0,r=0;
    if (argc < 4 || JS_ToFloat64(ctx,&x,argv[0]) || JS_ToFloat64(ctx,&y,argv[1]) ||
        JS_ToFloat64(ctx,&r,argv[2])) return JS_ThrowTypeError(ctx,"circle(x,y,r,rgba)");
    return Handle(ctx,MicroFxSceneAddFastCircle(script->scene,x,y,r,ColorArg(ctx,argv[3])));
}

static JSValue AddRoundedRect(JSContext *ctx, JSValueConst thisValue,
                              int argc, JSValueConst *argv)
{
    (void)thisValue;
    MicroFxScript *script = JS_GetContextOpaque(ctx);
    double x=0,y=0,w=0,h=0,r=0;
    if (argc < 6 || JS_ToFloat64(ctx,&x,argv[0]) || JS_ToFloat64(ctx,&y,argv[1]) ||
        JS_ToFloat64(ctx,&w,argv[2]) || JS_ToFloat64(ctx,&h,argv[3]) ||
        JS_ToFloat64(ctx,&r,argv[4])) return JS_ThrowTypeError(ctx,"sdfRoundedRect(x,y,w,h,r,rgba)");
    return Handle(ctx,MicroFxSceneAddRoundedRect(script->scene,x,y,w,h,r,ColorArg(ctx,argv[5])));
}

static JSValue AddRect(JSContext *ctx, JSValueConst thisValue,
                       int argc, JSValueConst *argv)
{
    (void)thisValue;
    MicroFxScript *script = JS_GetContextOpaque(ctx);
    double x=0,y=0,w=0,h=0;
    if (argc < 5 || JS_ToFloat64(ctx,&x,argv[0]) || JS_ToFloat64(ctx,&y,argv[1]) ||
        JS_ToFloat64(ctx,&w,argv[2]) || JS_ToFloat64(ctx,&h,argv[3]))
        return JS_ThrowTypeError(ctx,"rect(x,y,w,h,rgba)");
    return Handle(ctx,MicroFxSceneAddRect(script->scene,x,y,w,h,ColorArg(ctx,argv[4])));
}

static JSValue AddGradientRect(JSContext *ctx, JSValueConst thisValue,
                               int argc, JSValueConst *argv)
{
    (void)thisValue;
    MicroFxScript *script = JS_GetContextOpaque(ctx);
    double x=0,y=0,w=0,h=0;
    if (argc < 6 || JS_ToFloat64(ctx,&x,argv[0]) || JS_ToFloat64(ctx,&y,argv[1]) ||
        JS_ToFloat64(ctx,&w,argv[2]) || JS_ToFloat64(ctx,&h,argv[3]))
        return JS_ThrowTypeError(ctx,"gradientRect(x,y,w,h,topRgba,bottomRgba)");
    return Handle(ctx,MicroFxSceneAddGradientRect(script->scene,x,y,w,h,
                                                  ColorArg(ctx,argv[4]),
                                                  ColorArg(ctx,argv[5])));
}

static JSValue AddBackground(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);
    if(argc<2)return JS_ThrowTypeError(ctx,"background(topRgba,bottomRgba)");
    return Handle(ctx,MicroFxSceneAddBackground(script->scene,ColorArg(ctx,argv[0]),
                                                ColorArg(ctx,argv[1])));
}

static JSValue QrMatrix(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;
    if(argc!=1)return JS_ThrowTypeError(ctx,"qr(value,x,y,size[,foreground,background])");
    const char *value=JS_ToCString(ctx,argv[0]);
    if(!value)return JS_EXCEPTION;
    QRcode *code=QRcode_encodeString8bit(value,0,QR_ECLEVEL_M);
    JS_FreeCString(ctx,value);
    if(!code)return JS_ThrowInternalError(ctx,"QR encoding failed");
    size_t row=(size_t)code->width+1;
    size_t length=row*(size_t)code->width;
    char *matrix=malloc(length+1);
    if(!matrix){QRcode_free(code);return JS_ThrowOutOfMemory(ctx);}
    size_t cursor=0;
    for(int y=0;y<code->width;y++){
        for(int x=0;x<code->width;x++)
            matrix[cursor++]=(code->data[y*code->width+x]&1)?'1':'0';
        matrix[cursor++]='\n';
    }
    matrix[cursor]='\0';
    JSValue result=JS_NewStringLen(ctx,matrix,cursor);
    free(matrix);QRcode_free(code);
    return result;
}


static JSValue AddPrimitive(JSContext *ctx, int argc, JSValueConst *argv,
                            MicroFxMeshKind kind, const char *signature)
{
    MicroFxScript *script=JS_GetContextOpaque(ctx); double x=0,y=0,z=0,scale=0;
    if(argc<5||JS_ToFloat64(ctx,&x,argv[0])||JS_ToFloat64(ctx,&y,argv[1])||
       JS_ToFloat64(ctx,&z,argv[2])||JS_ToFloat64(ctx,&scale,argv[3]))
        return JS_ThrowTypeError(ctx,"%s",signature);
    uint32_t color=ColorArg(ctx,argv[4]);
    int handle=kind==MICROFX_MESH_SPHERE?MicroFxSceneAddSphere(script->scene,x,y,z,scale,color):
               kind==MICROFX_MESH_WIRE_CUBE?MicroFxSceneAddWireCube(script->scene,x,y,z,scale,color):
               kind==MICROFX_MESH_GRID?MicroFxSceneAddGrid(script->scene,x,y,z,scale,color):
               MicroFxSceneAddCube(script->scene,x,y,z,scale,color);
    return Handle(ctx,handle);
}

static JSValue AddCube(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{ (void)thisValue; return AddPrimitive(ctx,argc,argv,MICROFX_MESH_CUBE,"cube(x,y,z,size,rgba)"); }
static JSValue AddSphere(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{ (void)thisValue; return AddPrimitive(ctx,argc,argv,MICROFX_MESH_SPHERE,"sphere(x,y,z,size,rgba)"); }
static JSValue AddWireCube(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{ (void)thisValue; return AddPrimitive(ctx,argc,argv,MICROFX_MESH_WIRE_CUBE,"wireCube(x,y,z,size,rgba)"); }
static JSValue AddGrid(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{ (void)thisValue; return AddPrimitive(ctx,argc,argv,MICROFX_MESH_GRID,"grid(x,y,z,size,rgba)"); }

static JSValue AddModel(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;
    MicroFxScript *script=JS_GetContextOpaque(ctx);
    double x=0,y=0,z=0,scale=0;
    if(argc<6||JS_ToFloat64(ctx,&x,argv[1])||JS_ToFloat64(ctx,&y,argv[2])||
       JS_ToFloat64(ctx,&z,argv[3])||JS_ToFloat64(ctx,&scale,argv[4]))
        return JS_ThrowTypeError(ctx,"model(asset,x,y,z,size,rgba)");
    const char *asset=JS_ToCString(ctx,argv[0]);
    if(!asset)return JS_EXCEPTION;
    char path[MICROFX_MAX_ASSET_PATH];
    char error[128];
    bool resolved=MicroFxResolveAsset(script->projectRoot,asset,path,sizeof(path),
                                     error,sizeof(error));
    JS_FreeCString(ctx,asset);
    if(!resolved)return JS_ThrowReferenceError(ctx,"model asset rejected: %s",error);
    return Handle(ctx,MicroFxSceneAddModel(script->scene,path,x,y,z,scale,
                                          ColorArg(ctx,argv[5])));
}

static JSValue Transform(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);int32_t handle=0;
    double v[7]={0};
    if(argc<8||JS_ToInt32(ctx,&handle,argv[0]))return JS_ThrowTypeError(ctx,"transform(handle,x,y,z,rx,ry,rz,scale)");
    for(int i=0;i<7;i++)if(JS_ToFloat64(ctx,&v[i],argv[i+1]))return JS_ThrowTypeError(ctx,"transform arguments must be numbers");
    return JS_NewBool(ctx,MicroFxSceneTransform(script->scene,handle,v[0],v[1],v[2],v[3],v[4],v[5],v[6]));
}

static JSValue SetMeshShader(JSContext *ctx,JSValueConst thisValue,int argc,
                             JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);int32_t handle=0;
    if((argc!=2&&argc!=3)||JS_ToInt32(ctx,&handle,argv[0]))
        return JS_ThrowTypeError(ctx,"shader(handle,fragment) or shader(handle,vertex,fragment)");
    const char *vertex=argc==3?JS_ToCString(ctx,argv[1]):NULL;
    const char *fragment=JS_ToCString(ctx,argv[argc-1]);
    if((argc==3&&!vertex)||!fragment){
        if(vertex)JS_FreeCString(ctx,vertex);
        if(fragment)JS_FreeCString(ctx,fragment);
        return JS_EXCEPTION;
    }
    char vertexPath[MICROFX_MAX_ASSET_PATH]={0};
    char fragmentPath[MICROFX_MAX_ASSET_PATH]={0};
    char error[128];
    bool resolvedFragment=MicroFxResolveAsset(script->projectRoot,fragment,
        fragmentPath,sizeof(fragmentPath),error,sizeof(error));
    bool resolvedVertex=argc!=3||MicroFxResolveAsset(script->projectRoot,vertex,
        vertexPath,sizeof(vertexPath),error,sizeof(error));
    if(vertex)JS_FreeCString(ctx,vertex);
    JS_FreeCString(ctx,fragment);
    if(!resolvedFragment||!resolvedVertex)
        return JS_ThrowReferenceError(ctx,"mesh shader asset rejected: %s",error);
    if(!MicroFxSceneSetMeshShader(script->scene,handle,
                                  argc==3?vertexPath:"",fragmentPath))
        return JS_ThrowRangeError(ctx,"mesh shader could not be assigned");
    return JS_UNDEFINED;
}

static JSValue AddText(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);double x=0,y=0,size=0;
    if(argc<5||JS_ToFloat64(ctx,&x,argv[1])||JS_ToFloat64(ctx,&y,argv[2])||JS_ToFloat64(ctx,&size,argv[3]))
        return JS_ThrowTypeError(ctx,"text(value,x,y,size,rgba)");
    const char *value=JS_ToCString(ctx,argv[0]);if(!value)return JS_EXCEPTION;
    int handle=MicroFxSceneAddText(script->scene,value,x,y,size,ColorArg(ctx,argv[4]));
    JS_FreeCString(ctx,value);return Handle(ctx,handle);
}

static JSValue AddImage(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;
    MicroFxScript *script=JS_GetContextOpaque(ctx);
    double x=0,y=0,scale=0;
    if(argc!=5||JS_ToFloat64(ctx,&x,argv[1])||JS_ToFloat64(ctx,&y,argv[2])||
       JS_ToFloat64(ctx,&scale,argv[3]))
        return JS_ThrowTypeError(ctx,"image(asset,x,y,scale,tint) requires exactly 5 arguments");
    const char *asset=JS_ToCString(ctx,argv[0]);
    if(!asset)return JS_EXCEPTION;
    char path[MICROFX_MAX_ASSET_PATH];
    char error[128];
    bool resolved=MicroFxResolveAsset(script->projectRoot,asset,path,sizeof(path),
                                     error,sizeof(error));
    JS_FreeCString(ctx,asset);
    if(!resolved)return JS_ThrowReferenceError(ctx,"image asset rejected: %s",error);
    return Handle(ctx,MicroFxSceneAddImage(script->scene,path,x,y,scale,
                                          ColorArg(ctx,argv[4])));
}

static JSValue AddBackgroundImage(JSContext *ctx,JSValueConst thisValue,
                                  int argc,JSValueConst *argv)
{
    (void)thisValue;
    MicroFxScript *script=JS_GetContextOpaque(ctx);
    if(argc!=2)return JS_ThrowTypeError(ctx,"backgroundImage(asset,tint) requires exactly 2 arguments");
    const char *asset=JS_ToCString(ctx,argv[0]);
    if(!asset)return JS_EXCEPTION;
    char path[MICROFX_MAX_ASSET_PATH],error[128];
    bool resolved=MicroFxResolveAsset(script->projectRoot,asset,path,sizeof(path),
                                     error,sizeof(error));
    JS_FreeCString(ctx,asset);
    if(!resolved)return JS_ThrowReferenceError(ctx,"image asset rejected: %s",error);
    return Handle(ctx,MicroFxSceneAddBackgroundImage(script->scene,path,
                                                     ColorArg(ctx,argv[1])));
}

static JSValue SetImageScale(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);int32_t handle=0;double scale=0;
    if(argc<2||JS_ToInt32(ctx,&handle,argv[0])||JS_ToFloat64(ctx,&scale,argv[1]))
        return JS_ThrowTypeError(ctx,"imageScale(handle,scale)");
    return JS_NewBool(ctx,MicroFxSceneSetImageScale(script->scene,handle,(float)scale));
}

static JSValue SetText(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);int32_t handle=0;
    if(argc<2||JS_ToInt32(ctx,&handle,argv[0]))return JS_ThrowTypeError(ctx,"setText(handle,value)");
    const char *value=JS_ToCString(ctx,argv[1]);if(!value)return JS_EXCEPTION;
    bool ok=MicroFxSceneSetText(script->scene,handle,value);JS_FreeCString(ctx,value);
    return JS_NewBool(ctx,ok);
}

static JSValue SetFont(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);int32_t handle=0;
    if(argc<2||JS_ToInt32(ctx,&handle,argv[0]))return JS_ThrowTypeError(ctx,"font(handle,path)");
    const char *asset=JS_ToCString(ctx,argv[1]);if(!asset)return JS_EXCEPTION;
    if(!asset[0]){
        bool ok=MicroFxSceneSetTextFont(script->scene,handle,"");
        JS_FreeCString(ctx,asset);
        if(!ok)return JS_ThrowTypeError(ctx,"font() is only available on text elements");
        return JS_UNDEFINED;
    }
    char path[MICROFX_MAX_ASSET_PATH];char error[128];
    bool resolved=MicroFxResolveAsset(script->projectRoot,asset,path,sizeof(path),
                                     error,sizeof(error));
    JS_FreeCString(ctx,asset);
    if(!resolved)return JS_ThrowReferenceError(ctx,"font asset rejected: %s",error);
    if(!MicroFxSceneSetTextFont(script->scene,handle,path))
        return JS_ThrowTypeError(ctx,"font() is only available on text elements");
    return JS_UNDEFINED;
}

static JSValue SetTextAntialias(JSContext *ctx,JSValueConst thisValue,int argc,
                                JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);int32_t handle=0;
    if(argc<2||JS_ToInt32(ctx,&handle,argv[0]))
        return JS_ThrowTypeError(ctx,"textAntialias(handle,enabled)");
    if(!MicroFxSceneSetTextAntialias(script->scene,handle,JS_ToBool(ctx,argv[1])>0))
        return JS_ThrowTypeError(ctx,"antialias() is only available on text elements");
    return JS_UNDEFINED;
}

static JSValue SetColor(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);int32_t handle=0;
    if(argc<2||JS_ToInt32(ctx,&handle,argv[0]))return JS_ThrowTypeError(ctx,"color(handle,rgba)");
    return JS_NewBool(ctx,MicroFxSceneSetColor(script->scene,handle,ColorArg(ctx,argv[1])));
}

static JSValue SetVisible(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);int32_t handle=0;
    if(argc<2||JS_ToInt32(ctx,&handle,argv[0]))return JS_ThrowTypeError(ctx,"visible(handle,value)");
    return JS_NewBool(ctx,MicroFxSceneSetVisible(script->scene,handle,JS_ToBool(ctx,argv[1])>0));
}

static JSValue SetOpacity(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);int32_t handle=0;
    double opacity=1.0;
    if(argc<2||JS_ToInt32(ctx,&handle,argv[0])||JS_ToFloat64(ctx,&opacity,argv[1]))
        return JS_ThrowTypeError(ctx,"opacity(handle,value)");
    if(opacity<0.0||opacity>1.0)return JS_ThrowRangeError(ctx,"opacity must be 0..1");
    if(!MicroFxSceneSetOpacity(script->scene,handle,(float)opacity))
        return JS_ThrowTypeError(ctx,"opacity is available on 2D elements");
    return JS_UNDEFINED;
}

static JSValue SetEffect(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);int32_t handle=0,effect=0;
    double amount=1.0,scale=4.0;
    if(argc<2||JS_ToInt32(ctx,&handle,argv[0])||JS_ToInt32(ctx,&effect,argv[1])||
       (argc>2&&JS_ToFloat64(ctx,&amount,argv[2]))||(argc>3&&JS_ToFloat64(ctx,&scale,argv[3])))
        return JS_ThrowTypeError(ctx,"effect(handle,kind,amount=1,scale=4)");
    return JS_NewBool(ctx,MicroFxSceneSetEffect(script->scene,handle,effect,(float)amount,(float)scale));
}

static JSValue SetCamera(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);double v[7]={0};
    if(argc<7)return JS_ThrowTypeError(ctx,"camera(x,y,z,targetX,targetY,targetZ,fovY)");
    for(int i=0;i<7;i++)if(JS_ToFloat64(ctx,&v[i],argv[i]))return JS_ThrowTypeError(ctx,"camera arguments must be numbers");
    MicroFxSceneSetCamera(script->scene,v[0],v[1],v[2],v[3],v[4],v[5],v[6]);
    return JS_UNDEFINED;
}

static bool NumberProperty(JSContext *ctx,JSValueConst object,const char *name,double *value)
{
    JSValue property=JS_GetPropertyStr(ctx,object,name);
    if(JS_IsUndefined(property)){JS_FreeValue(ctx,property);return true;}
    bool ok=JS_ToFloat64(ctx,value,property)==0;JS_FreeValue(ctx,property);return ok;
}

static int StringProperty(JSContext *ctx,JSValueConst object,const char *name,
                          const char **value,JSValue *property)
{
    *property=JS_GetPropertyStr(ctx,object,name);
    if(JS_IsUndefined(*property))return 0;
    if(!JS_IsString(*property))return -1;
    *value=JS_ToCString(ctx,*property);
    return *value?1:-1;
}

static JSValue Configure(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);
    if(argc<1||!JS_IsObject(argv[0]))return JS_ThrowTypeError(ctx,"configure(settings)");
    MicroFxRuntimeSettings settings=script->scene->runtime;
    double outputWidth=settings.outputWidth,outputHeight=settings.outputHeight;
    double targetFps=settings.targetFps,minimum=settings.minimumPixelDensity;
    double profileInterval=settings.profileIntervalFrames;
    double densitySampleFrames=settings.densitySampleFrames;
    double densityStep=settings.densityStep;
    double densityDownThreshold=settings.densityDownThreshold;
    double densityUpThreshold=settings.densityUpThreshold;
    double densityUpSamples=settings.densityUpSamples;
    double duration=settings.durationSeconds;
    double depthBits=settings.depthBits;
    if(!NumberProperty(ctx,argv[0],"outputWidth",&outputWidth)||
       !NumberProperty(ctx,argv[0],"outputHeight",&outputHeight)||
       !NumberProperty(ctx,argv[0],"targetFps",&targetFps)||
       !NumberProperty(ctx,argv[0],"minimumPixelDensity",&minimum)||
       !NumberProperty(ctx,argv[0],"durationSeconds",&duration)||
       !NumberProperty(ctx,argv[0],"profileIntervalFrames",&profileInterval)||
       !NumberProperty(ctx,argv[0],"densitySampleFrames",&densitySampleFrames)||
       !NumberProperty(ctx,argv[0],"densityStep",&densityStep)||
       !NumberProperty(ctx,argv[0],"densityDownThreshold",&densityDownThreshold)||
       !NumberProperty(ctx,argv[0],"densityUpThreshold",&densityUpThreshold)||
       !NumberProperty(ctx,argv[0],"densityUpSamples",&densityUpSamples))
        return JS_ThrowTypeError(ctx,"configure numeric setting is invalid");
    const char *value=NULL;JSValue property;
    int present=StringProperty(ctx,argv[0],"quality",&value,&property);
    if(present<0){JS_FreeValue(ctx,property);return JS_ThrowTypeError(ctx,"quality must be performance, balanced, or quality");}
    if(present){
        if(strcmp(value,"performance")==0){settings.colorFormat=MICROFX_COLOR_RGB565;settings.depthBits=16;settings.dithering=true;settings.antialiasing=MICROFX_ANTIALIAS_NONE;}
        else if(strcmp(value,"balanced")==0){settings.colorFormat=MICROFX_COLOR_RGB565;settings.depthBits=24;settings.dithering=true;settings.antialiasing=MICROFX_ANTIALIAS_NONE;}
        else if(strcmp(value,"quality")==0){settings.colorFormat=MICROFX_COLOR_RGBA8888;settings.depthBits=24;settings.dithering=true;settings.antialiasing=MICROFX_ANTIALIAS_MSAA4;}
        else {JS_FreeCString(ctx,value);JS_FreeValue(ctx,property);return JS_ThrowRangeError(ctx,"quality must be performance, balanced, or quality");}
        JS_FreeCString(ctx,value);
    }
    JS_FreeValue(ctx,property);
    depthBits=settings.depthBits;
    if(!NumberProperty(ctx,argv[0],"depthBits",&depthBits))
        return JS_ThrowTypeError(ctx,"depthBits must be 16 or 24");
    present=StringProperty(ctx,argv[0],"colorFormat",&value,&property);
    if(present<0){JS_FreeValue(ctx,property);return JS_ThrowTypeError(ctx,"colorFormat must be rgb565 or rgba8888");}
    if(present){
        if(strcmp(value,"rgb565")==0)settings.colorFormat=MICROFX_COLOR_RGB565;
        else if(strcmp(value,"rgba8888")==0)settings.colorFormat=MICROFX_COLOR_RGBA8888;
        else {JS_FreeCString(ctx,value);JS_FreeValue(ctx,property);return JS_ThrowRangeError(ctx,"colorFormat must be rgb565 or rgba8888");}
        JS_FreeCString(ctx,value);
    }
    JS_FreeValue(ctx,property);
    present=StringProperty(ctx,argv[0],"antialiasing",&value,&property);
    if(present<0){JS_FreeValue(ctx,property);return JS_ThrowTypeError(ctx,"antialiasing must be none or msaa4");}
    if(present){
        if(strcmp(value,"none")==0)settings.antialiasing=MICROFX_ANTIALIAS_NONE;
        else if(strcmp(value,"msaa4")==0)settings.antialiasing=MICROFX_ANTIALIAS_MSAA4;
        else {JS_FreeCString(ctx,value);JS_FreeValue(ctx,property);return JS_ThrowRangeError(ctx,"antialiasing must be none or msaa4");}
        JS_FreeCString(ctx,value);
    }
    JS_FreeValue(ctx,property);
    JSValue density=JS_GetPropertyStr(ctx,argv[0],"pixelDensity");
    if(!JS_IsUndefined(density)){
        if(JS_IsString(density)){
            const char *value=JS_ToCString(ctx,density);
            if(!value||strcmp(value,"auto")!=0){if(value)JS_FreeCString(ctx,value);JS_FreeValue(ctx,density);return JS_ThrowRangeError(ctx,"pixelDensity must be auto or 0.25..1");}
            JS_FreeCString(ctx,value);settings.automaticDensity=true;settings.pixelDensity=1.0f;
        }else{
            double value=0;if(JS_ToFloat64(ctx,&value,density)){JS_FreeValue(ctx,density);return JS_ThrowTypeError(ctx,"pixelDensity must be auto or a number");}
            settings.automaticDensity=false;settings.pixelDensity=(float)value;
        }
    }
    JS_FreeValue(ctx,density);
    JSValue debug=JS_GetPropertyStr(ctx,argv[0],"debugBar");
    if(!JS_IsUndefined(debug)){
        if(JS_IsBool(debug))settings.debugBarUntilSeconds=JS_ToBool(ctx,debug)>0?-1.0f:0.0f;
        else{
            double minutes=0;
            if(JS_ToFloat64(ctx,&minutes,debug)||!isfinite(minutes)||minutes<0.0){
                JS_FreeValue(ctx,debug);
                return JS_ThrowRangeError(ctx,"debugBar must be a boolean or non-negative minutes");
            }
            settings.debugBarUntilSeconds=(float)(minutes*60.0);
        }
    }
    JS_FreeValue(ctx,debug);
    JSValue profiling=JS_GetPropertyStr(ctx,argv[0],"profiling");
    if(!JS_IsUndefined(profiling))settings.profiling=JS_ToBool(ctx,profiling)>0;
    JS_FreeValue(ctx,profiling);
    JSValue dithering=JS_GetPropertyStr(ctx,argv[0],"dithering");
    if(!JS_IsUndefined(dithering))settings.dithering=JS_ToBool(ctx,dithering)>0;
    JS_FreeValue(ctx,dithering);
    settings.outputWidth=(int)outputWidth;settings.outputHeight=(int)outputHeight;
    settings.targetFps=(int)targetFps;settings.minimumPixelDensity=(float)minimum;
    settings.durationSeconds=(float)duration;
    settings.profileIntervalFrames=(int)profileInterval;
    settings.densitySampleFrames=(int)densitySampleFrames;
    settings.densityStep=(float)densityStep;
    settings.densityDownThreshold=(float)densityDownThreshold;
    settings.densityUpThreshold=(float)densityUpThreshold;
    settings.densityUpSamples=(int)densityUpSamples;
    settings.depthBits=(int)depthBits;
    if(settings.targetFps<1||settings.minimumPixelDensity<0.25f||settings.minimumPixelDensity>1.0f||
       settings.pixelDensity<settings.minimumPixelDensity||settings.pixelDensity>1.0f||
       settings.durationSeconds<0.0f||settings.profileIntervalFrames<30||
       settings.densitySampleFrames<30||
       settings.densityStep<0.01f||settings.densityStep>0.25f||
       settings.densityDownThreshold<1.0f||settings.densityDownThreshold>2.0f||
       settings.densityUpThreshold<0.25f||
       settings.densityUpThreshold>=settings.densityDownThreshold||
       settings.densityUpSamples<1||settings.densityUpSamples>100||
       (settings.depthBits!=16&&settings.depthBits!=24)||
       settings.outputWidth<0||settings.outputHeight<0||
       ((settings.outputWidth==0)!=(settings.outputHeight==0)))
        return JS_ThrowRangeError(ctx,"invalid output, density, or target FPS settings");
    settings.configured=true;script->scene->runtime=settings;return JS_UNDEFINED;
}

static JSValue Environment(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;
    if(argc<1)return JS_ThrowTypeError(ctx,"env(name, fallback='')");
    const char *name=JS_ToCString(ctx,argv[0]);
    if(!name)return JS_EXCEPTION;
    const char *value=getenv(name);
    JSValue result;
    if(value){
        result=JS_NewString(ctx,value);
    }else if(argc>1){
        const char *fallback=JS_ToCString(ctx,argv[1]);
        if(!fallback){JS_FreeCString(ctx,name);return JS_EXCEPTION;}
        result=JS_NewString(ctx,fallback);
        JS_FreeCString(ctx,fallback);
    }else{
        result=JS_NewString(ctx,"");
    }
    JS_FreeCString(ctx,name);
    return result;
}

static JSValue ProjectData(JSContext *ctx,JSValueConst thisValue,int argc,
                           JSValueConst *argv)
{
    (void)thisValue;
    MicroFxScript *script=JS_GetContextOpaque(ctx);
    if(argc<1)return JS_ThrowTypeError(ctx,"data(asset, fallback=undefined)");
    const char *asset=JS_ToCString(ctx,argv[0]);
    if(!asset)return JS_EXCEPTION;
    char path[MICROFX_MAX_ASSET_PATH],error[128];
    bool resolved=MicroFxResolveDataAsset(script->projectRoot,asset,path,sizeof(path),
                                          error,sizeof(error));
    JS_FreeCString(ctx,asset);
    if(!resolved){
        if(argc>1)return JS_DupValue(ctx,argv[1]);
        return JS_ThrowReferenceError(ctx,"data asset rejected: %s",error);
    }
    FILE *file=fopen(path,"rb");
    if(!file){
        if(argc>1)return JS_DupValue(ctx,argv[1]);
        return JS_ThrowReferenceError(ctx,"data asset cannot be opened");
    }
    enum { MAX_DATA_BYTES=64*1024 };
    char *source=malloc(MAX_DATA_BYTES+1);
    size_t size=source?fread(source,1,MAX_DATA_BYTES+1,file):0;
    bool failed=ferror(file)!=0;
    fclose(file);
    if(!source||failed||size>MAX_DATA_BYTES){
        free(source);
        return JS_ThrowRangeError(ctx,"data asset exceeds 64 KiB or cannot be read");
    }
    source[size]='\0';
    JSValue result=JS_ParseJSON(ctx,source,size,path);
    free(source);
    return result;
}

static JSValue DebugBar(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;MicroFxScript *script=JS_GetContextOpaque(ctx);
    if(argc<1)return JS_ThrowTypeError(ctx,"debugBar(booleanOrMinutes)");
    if(JS_IsBool(argv[0])){
        script->scene->runtime.debugBarUntilSeconds=JS_ToBool(ctx,argv[0])>0?-1.0f:0.0f;
    }else{
        double minutes=0;
        if(JS_ToFloat64(ctx,&minutes,argv[0])||!isfinite(minutes)||minutes<0.0)
            return JS_ThrowRangeError(ctx,"debugBar must be a boolean or non-negative minutes");
        script->scene->runtime.debugBarUntilSeconds=minutes==0.0?0.0f:
            script->scene->time+(float)(minutes*60.0);
    }
    return JS_UNDEFINED;
}

static float Hash2(float x,float y)
{
    float value=sinf(x*127.1f+y*311.7f)*43758.5453f;
    return value-floorf(value);
}

static JSValue MathHash(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;double x=0,y=0;if(argc<2||JS_ToFloat64(ctx,&x,argv[0])||JS_ToFloat64(ctx,&y,argv[1]))return JS_ThrowTypeError(ctx,"hash2(x,y)");
    return JS_NewFloat64(ctx,Hash2((float)x,(float)y));
}

static JSValue MathNoise(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;double xd=0,yd=0;if(argc<2||JS_ToFloat64(ctx,&xd,argv[0])||JS_ToFloat64(ctx,&yd,argv[1]))return JS_ThrowTypeError(ctx,"noise2(x,y)");
    float x=(float)xd,y=(float)yd,ix=floorf(x),iy=floorf(y),fx=x-ix,fy=y-iy;
    fx=fx*fx*(3.0f-2.0f*fx);fy=fy*fy*(3.0f-2.0f*fy);
    float a=Hash2(ix,iy),b=Hash2(ix+1,iy),c=Hash2(ix,iy+1),d=Hash2(ix+1,iy+1);
    return JS_NewFloat64(ctx,(a+(b-a)*fx)+((c+(d-c)*fx)-(a+(b-a)*fx))*fy);
}

static JSValue MathLerp(JSContext *ctx,JSValueConst thisValue,int argc,JSValueConst *argv)
{
    (void)thisValue;double a=0,b=0,t=0;if(argc<3||JS_ToFloat64(ctx,&a,argv[0])||JS_ToFloat64(ctx,&b,argv[1])||JS_ToFloat64(ctx,&t,argv[2]))return JS_ThrowTypeError(ctx,"lerp(a,b,t)");
    return JS_NewFloat64(ctx,a+(b-a)*t);
}

static JSValue Move(JSContext *ctx, JSValueConst thisValue,
                    int argc, JSValueConst *argv)
{
    (void)thisValue;
    MicroFxScript *script = JS_GetContextOpaque(ctx);
    int32_t handle=0; double x=0,y=0,rotation=0;
    if (argc < 4 || JS_ToInt32(ctx,&handle,argv[0]) || JS_ToFloat64(ctx,&x,argv[1]) ||
        JS_ToFloat64(ctx,&y,argv[2]) || JS_ToFloat64(ctx,&rotation,argv[3]))
        return JS_ThrowTypeError(ctx,"move(handle,x,y,rotation)");
    return JS_NewBool(ctx,MicroFxSceneMove(script->scene,handle,x,y,rotation));
}

static void DumpException(JSContext *ctx)
{
    JSValue exception = JS_GetException(ctx);
    const char *message = JS_ToCString(ctx, exception);
    JSValue stackValue=JS_GetPropertyStr(ctx,exception,"stack");
    const char *stack=JS_IsUndefined(stackValue)?NULL:JS_ToCString(ctx,stackValue);
    const char *detail=stack&&stack[0]?stack:(message?message:"unknown exception");
    fprintf(stderr,"MICROFX_JS_ERROR ");
    for(const char *cursor=detail;*cursor;cursor++)
        fputc((*cursor=='\n'||*cursor=='\r'||*cursor=='\t')?' ':*cursor,stderr);
    fputc('\n',stderr);
    if(stack)JS_FreeCString(ctx,stack);
    JS_FreeValue(ctx,stackValue);
    JS_FreeCString(ctx, message);
    JS_FreeValue(ctx, exception);
}

MicroFxScript *MicroFxScriptCreate(MicroFxScene *scene, const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) { fprintf(stderr,"MICROFX_JS script not found: %s\n",path); return NULL; }
    fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
    char *source = malloc((size_t)size+1);
    if (!source || fread(source,1,(size_t)size,file)!=(size_t)size) {
        fclose(file); free(source); return NULL;
    }
    fclose(file); source[size]='\0';
    MicroFxScript *script=calloc(1,sizeof(*script));
    if(!script){free(source);return NULL;}
    script->update=JS_UNDEFINED;
    script->beginFrame=JS_UNDEFINED;
    script->endFrame=JS_UNDEFINED;
    char rootError[128];
    if(!MicroFxProjectRoot(path,script->projectRoot,sizeof(script->projectRoot),
                          rootError,sizeof(rootError))){
        fprintf(stderr,"MICROFX_ASSET %s\n",rootError);
        free(source);free(script);return NULL;
    }
    script->scene=scene; script->runtime=JS_NewRuntime(); script->context=JS_NewContext(script->runtime);
    JS_SetMemoryLimit(script->runtime, 16*1024*1024);
    JS_SetMaxStackSize(script->runtime, 512*1024);
    JS_SetContextOpaque(script->context,script);
    JSValue global=JS_GetGlobalObject(script->context);
    JSValue fx=JS_NewObject(script->context);
    JS_SetPropertyStr(script->context,fx,"_circle",JS_NewCFunction(script->context,AddFastCircle,"_circle",4));
    JS_SetPropertyStr(script->context,fx,"_sdfCircle",JS_NewCFunction(script->context,AddSdfCircle,"_sdfCircle",4));
    JS_SetPropertyStr(script->context,fx,"_sdfRoundedRect",JS_NewCFunction(script->context,AddRoundedRect,"_sdfRoundedRect",6));
    JS_SetPropertyStr(script->context,fx,"_rect",JS_NewCFunction(script->context,AddRect,"_rect",5));
    JS_SetPropertyStr(script->context,fx,"_gradientRect",JS_NewCFunction(script->context,AddGradientRect,"_gradientRect",6));
    JS_SetPropertyStr(script->context,fx,"_background",JS_NewCFunction(script->context,AddBackground,"_background",2));
    JS_SetPropertyStr(script->context,fx,"_qrMatrix",JS_NewCFunction(script->context,QrMatrix,"_qrMatrix",1));
    JS_SetPropertyStr(script->context,fx,"_move",JS_NewCFunction(script->context,Move,"_move",4));
    JS_SetPropertyStr(script->context,fx,"_cube",JS_NewCFunction(script->context,AddCube,"_cube",5));
    JS_SetPropertyStr(script->context,fx,"_sphere",JS_NewCFunction(script->context,AddSphere,"_sphere",5));
    JS_SetPropertyStr(script->context,fx,"_wireCube",JS_NewCFunction(script->context,AddWireCube,"_wireCube",5));
    JS_SetPropertyStr(script->context,fx,"_grid",JS_NewCFunction(script->context,AddGrid,"_grid",5));
    JS_SetPropertyStr(script->context,fx,"_model",JS_NewCFunction(script->context,AddModel,"_model",6));
    JS_SetPropertyStr(script->context,fx,"_transform",JS_NewCFunction(script->context,Transform,"_transform",8));
    JS_SetPropertyStr(script->context,fx,"_shader",JS_NewCFunction(script->context,SetMeshShader,"_shader",3));
    JS_SetPropertyStr(script->context,fx,"_text",JS_NewCFunction(script->context,AddText,"_text",5));
    JS_SetPropertyStr(script->context,fx,"_image",JS_NewCFunction(script->context,AddImage,"_image",5));
    JS_SetPropertyStr(script->context,fx,"_backgroundImage",JS_NewCFunction(script->context,AddBackgroundImage,"_backgroundImage",2));
    JS_SetPropertyStr(script->context,fx,"_imageScale",JS_NewCFunction(script->context,SetImageScale,"_imageScale",2));
    JS_SetPropertyStr(script->context,fx,"_setText",JS_NewCFunction(script->context,SetText,"_setText",2));
    JS_SetPropertyStr(script->context,fx,"_font",JS_NewCFunction(script->context,SetFont,"_font",2));
    JS_SetPropertyStr(script->context,fx,"_textAntialias",JS_NewCFunction(script->context,SetTextAntialias,"_textAntialias",2));
    JS_SetPropertyStr(script->context,fx,"_color",JS_NewCFunction(script->context,SetColor,"_color",2));
    JS_SetPropertyStr(script->context,fx,"_visible",JS_NewCFunction(script->context,SetVisible,"_visible",2));
    JS_SetPropertyStr(script->context,fx,"_opacity",JS_NewCFunction(script->context,SetOpacity,"_opacity",2));
    JS_SetPropertyStr(script->context,fx,"_effect",JS_NewCFunction(script->context,SetEffect,"_effect",4));
    JS_SetPropertyStr(script->context,fx,"camera",JS_NewCFunction(script->context,SetCamera,"camera",7));
    JS_SetPropertyStr(script->context,fx,"configure",JS_NewCFunction(script->context,Configure,"configure",1));
    JS_SetPropertyStr(script->context,fx,"debugBar",JS_NewCFunction(script->context,DebugBar,"debugBar",1));
    JS_SetPropertyStr(script->context,fx,"env",JS_NewCFunction(script->context,Environment,"env",2));
    JS_SetPropertyStr(script->context,fx,"data",JS_NewCFunction(script->context,ProjectData,"data",2));
    script->network=MicroFxNetworkCreate(script->context,fx);
    if(!script->network){
        fprintf(stderr,"MICROFX_NET initialization failed\n");
        JS_FreeValue(script->context,fx);JS_FreeValue(script->context,global);
        free(source);MicroFxScriptDestroy(script);return NULL;
    }
    JSValue product=JS_NewObject(script->context);
    JS_SetPropertyStr(script->context,product,"name",JS_NewString(script->context,MICROFX_PRODUCT_NAME));
    JS_SetPropertyStr(script->context,product,"slug",JS_NewString(script->context,MICROFX_PRODUCT_SLUG));
    JS_SetPropertyStr(script->context,product,"defaultPeerId",JS_NewString(script->context,MICROFX_DEFAULT_PEER_ID));
    JS_SetPropertyStr(script->context,product,"defaultSetupSsid",JS_NewString(script->context,MICROFX_DEFAULT_SETUP_SSID));
    JS_SetPropertyStr(script->context,product,"defaultSetupPassword",JS_NewString(script->context,MICROFX_DEFAULT_SETUP_PASSWORD));
    JS_SetPropertyStr(script->context,fx,"product",product);
    JSValue math=JS_NewObject(script->context);
    JS_SetPropertyStr(script->context,math,"hash2",JS_NewCFunction(script->context,MathHash,"hash2",2));
    JS_SetPropertyStr(script->context,math,"noise2",JS_NewCFunction(script->context,MathNoise,"noise2",2));
    JS_SetPropertyStr(script->context,math,"lerp",JS_NewCFunction(script->context,MathLerp,"lerp",3));
    JS_SetPropertyStr(script->context,fx,"math",math);
    JSValue effects=JS_NewObject(script->context);
    JS_SetPropertyStr(script->context,effects,"solid",JS_NewInt32(script->context,0));
    JS_SetPropertyStr(script->context,effects,"gradient",JS_NewInt32(script->context,1));
    JS_SetPropertyStr(script->context,effects,"noise",JS_NewInt32(script->context,2));
    JS_SetPropertyStr(script->context,effects,"bands",JS_NewInt32(script->context,3));
    JS_SetPropertyStr(script->context,fx,"effects",effects);
    JS_SetPropertyStr(script->context,fx,"width",JS_NewInt32(script->context,MICROFX_DESIGN_WIDTH));
    JS_SetPropertyStr(script->context,fx,"height",JS_NewInt32(script->context,MICROFX_DESIGN_HEIGHT));
    JS_SetPropertyStr(script->context,global,"width",JS_NewInt32(script->context,MICROFX_DESIGN_WIDTH));
    JS_SetPropertyStr(script->context,global,"height",JS_NewInt32(script->context,MICROFX_DESIGN_HEIGHT));
    JS_SetPropertyStr(script->context,global,"fx",fx);
    JS_FreeValue(script->context,global);
    JSValue runtimeResult=JS_Eval(script->context,MICROFX_RUNTIME_JS,strlen(MICROFX_RUNTIME_JS),
                                  "microfx-runtime.js",JS_EVAL_TYPE_GLOBAL);
    if(JS_IsException(runtimeResult)){
        DumpException(script->context);JS_FreeValue(script->context,runtimeResult);
        free(source);MicroFxScriptDestroy(script);return NULL;
    }
    JS_FreeValue(script->context,runtimeResult);
    global=JS_GetGlobalObject(script->context);
    JSValue runtimeFx=JS_GetPropertyStr(script->context,global,"fx");
    script->beginFrame=JS_GetPropertyStr(script->context,runtimeFx,"_beginFrame");
    script->endFrame=JS_GetPropertyStr(script->context,runtimeFx,"_endFrame");
    JS_FreeValue(script->context,runtimeFx);
    JS_FreeValue(script->context,global);
    JSValue result=JS_Eval(script->context,source,(size_t)size,path,JS_EVAL_TYPE_GLOBAL);
    free(source);
    if (JS_IsException(result)) { DumpException(script->context); JS_FreeValue(script->context,result); MicroFxScriptDestroy(script); return NULL; }
    JS_FreeValue(script->context,result);
    global=JS_GetGlobalObject(script->context);
    script->update=JS_GetPropertyStr(script->context,global,"update");
    JS_FreeValue(script->context,global);
    if (!JS_IsFunction(script->context,script->update)) { fprintf(stderr,"MICROFX_JS update(time,delta) missing\n"); MicroFxScriptDestroy(script); return NULL; }
    printf("MICROFX_JS loaded=%s sdf=%d quads=%d mesh=%d text=%d image=%d\n", path,
           scene->sdfCount, scene->quadCount, scene->meshCount, scene->textCount,
           scene->imageCount);
    return script;
}

bool MicroFxScriptUpdate(MicroFxScript *script, double time, double delta)
{
    if (!script) return false;
    if(!MicroFxNetworkPump(script->network)){DumpException(script->context);return false;}
    script->scene->time=(float)time;
    JSValue frameResult=JS_Call(script->context,script->beginFrame,JS_UNDEFINED,0,NULL);
    if(JS_IsException(frameResult)){DumpException(script->context);JS_FreeValue(script->context,frameResult);return false;}
    JS_FreeValue(script->context,frameResult);
    JSValue args[2]={JS_NewFloat64(script->context,time),JS_NewFloat64(script->context,delta)};
    JSValue result=JS_Call(script->context,script->update,JS_UNDEFINED,2,args);
    JS_FreeValue(script->context,args[0]); JS_FreeValue(script->context,args[1]);
    if (JS_IsException(result)) { DumpException(script->context); JS_FreeValue(script->context,result); return false; }
    JS_FreeValue(script->context,result);
    frameResult=JS_Call(script->context,script->endFrame,JS_UNDEFINED,0,NULL);
    if(JS_IsException(frameResult)){DumpException(script->context);JS_FreeValue(script->context,frameResult);return false;}
    JS_FreeValue(script->context,frameResult);
    return true;
}

void MicroFxScriptDestroy(MicroFxScript *script)
{
    if (!script) return;
    if (script->network) MicroFxNetworkDestroy(script->network);
    if (script->context) JS_FreeValue(script->context,script->update);
    if (script->context) JS_FreeValue(script->context,script->beginFrame);
    if (script->context) JS_FreeValue(script->context,script->endFrame);
    if (script->context) JS_FreeContext(script->context);
    if (script->runtime) JS_FreeRuntime(script->runtime);
    free(script);
}
