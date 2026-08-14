#include "microfx/image_renderer.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    float position[2];
    float uv[2];
} ImageVertex;

static GLuint Compile(GLenum type, const char *source)
{
    GLuint shader=glCreateShader(type);
    glShaderSource(shader,1,&source,NULL);
    glCompileShader(shader);
    GLint ok=GL_FALSE;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){
        char log[1024]={0};
        glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        fprintf(stderr,"MICROFX_IMAGE shader failure: %s\n",log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool MicroFxImageRendererInit(MicroFxImageRenderer *renderer)
{
    *renderer=(MicroFxImageRenderer){0};
    static const char *vs=
        "#version 100\nprecision highp float;attribute vec2 aPosition;"
        "attribute vec2 aUv;uniform vec2 uViewport;uniform vec4 uTransform;"
        "varying mediump vec2 vUv;"
        "void main(){vec2 local=vec2(uTransform.z*aPosition.x-uTransform.w*aPosition.y,"
        "uTransform.w*aPosition.x+uTransform.z*aPosition.y);"
        "vec2 world=local+uTransform.xy;"
        "vec2 p=vec2(world.x/uViewport.x*2.0-1.0,"
        "1.0-world.y/uViewport.y*2.0);gl_Position=vec4(p,0.0,1.0);"
        "vUv=aUv;}\n";
    static const char *fs=
        "#version 100\nprecision lowp float;varying mediump vec2 vUv;"
        "uniform sampler2D uTexture;uniform vec4 uColor;"
        "void main(){gl_FragColor=texture2D(uTexture,vUv)*uColor;}\n";
    GLuint vertex=Compile(GL_VERTEX_SHADER,vs);
    GLuint fragment=Compile(GL_FRAGMENT_SHADER,fs);
    if(!vertex||!fragment){
        if(vertex)glDeleteShader(vertex);
        if(fragment)glDeleteShader(fragment);
        return false;
    }
    renderer->program=glCreateProgram();
    glAttachShader(renderer->program,vertex);
    glAttachShader(renderer->program,fragment);
    glBindAttribLocation(renderer->program,0,"aPosition");
    glBindAttribLocation(renderer->program,1,"aUv");
    glLinkProgram(renderer->program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked=GL_FALSE;
    glGetProgramiv(renderer->program,GL_LINK_STATUS,&linked);
    if(!linked){
        glDeleteProgram(renderer->program);
        renderer->program=0;
        return false;
    }
    renderer->viewportLocation=glGetUniformLocation(renderer->program,"uViewport");
    renderer->textureLocation=glGetUniformLocation(renderer->program,"uTexture");
    renderer->transformLocation=glGetUniformLocation(renderer->program,"uTransform");
    renderer->colorLocation=glGetUniformLocation(renderer->program,"uColor");
    glGenBuffers(1,&renderer->vertexBuffer);
    return renderer->vertexBuffer!=0;
}

static bool ResolveTextures(MicroFxImageRenderer *renderer,MicroFxScene *scene)
{
    for(int i=0;i<scene->imageCount;i++){
        const char *path=scene->image[i].assetPath;
        int found=-1;
        for(int j=0;j<renderer->textureCount;j++){
            if(strcmp(renderer->texturePaths[j],path)==0){found=j;break;}
        }
        if(found<0){
            if(renderer->textureCount>=MICROFX_MAX_IMAGE_ELEMENTS)return false;
            Image image=LoadImage(path);
            if(!IsImageValid(image)){
                fprintf(stderr,"MICROFX_IMAGE failed to load asset: %s\n",path);
                return false;
            }
            // The appliance scanout is RGB565. Keeping opaque source images as
            // RGB888 spends 50% more texture bandwidth without adding output
            // precision, which is costly for full-screen images on GC880.
            if(image.format==PIXELFORMAT_UNCOMPRESSED_R8G8B8){
                ImageFormat(&image,PIXELFORMAT_UNCOMPRESSED_R5G6B5);
            }
            found=renderer->textureCount++;
            renderer->textures[found]=LoadTextureFromImage(image);
            UnloadImage(image);
            if(!IsTextureValid(renderer->textures[found])){
                fprintf(stderr,"MICROFX_IMAGE failed to upload texture: %s\n",path);
                return false;
            }
            // Images commonly move and rotate through fractional pixels. Point
            // sampling makes high-contrast features shimmer between texels;
            // bilinear sampling provides stable reconstruction without the
            // memory cost of a mip chain for near-native-size artwork.
            glBindTexture(GL_TEXTURE_2D,renderer->textures[found].id);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D,0);
            snprintf(renderer->texturePaths[found],MICROFX_MAX_ASSET_PATH,"%s",path);
        }
        renderer->textureIndex[i]=found;
    }
    return true;
}

static void SetVertex(ImageVertex *vertex,float localX,float localY,float u,float v)
{
    vertex->position[0]=localX;
    vertex->position[1]=localY;
    vertex->uv[0]=u;vertex->uv[1]=v;
}

static void Rebuild(MicroFxImageRenderer *renderer,MicroFxScene *scene)
{
    ImageVertex vertices[MICROFX_MAX_IMAGE_ELEMENTS*6];
    for(int i=0;i<scene->imageCount;i++){
        const MicroFxImageElement *e=&scene->image[i];
        const Texture2D texture=renderer->textures[renderer->textureIndex[i]];
        float width=texture.width*e->scale,height=texture.height*e->scale;
        float left=-width*0.5f,right=width*0.5f;
        float top=-height*0.5f,bottom=height*0.5f;
        ImageVertex *v=&vertices[i*6];
        SetVertex(&v[0],left,top,0,0);SetVertex(&v[1],right,top,1,0);
        SetVertex(&v[2],right,bottom,1,1);SetVertex(&v[3],left,top,0,0);
        SetVertex(&v[4],right,bottom,1,1);SetVertex(&v[5],left,bottom,0,1);
    }
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(scene->imageCount*6*sizeof(*vertices)),
                 vertices,GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    scene->imageDirty=false;
}

static bool TextureFormatHasAlpha(int format)
{
    switch(format){
    case PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
    case PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
    case PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:
    case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
    case PIXELFORMAT_COMPRESSED_DXT1_RGBA:
    case PIXELFORMAT_COMPRESSED_DXT3_RGBA:
    case PIXELFORMAT_COMPRESSED_DXT5_RGBA:
    case PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA:
    case PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA:
    case PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA:
        return true;
    default:
        return false;
    }
}

bool MicroFxImageRendererDraw(MicroFxImageRenderer *renderer,
                              MicroFxScene *scene,int width,int height)
{
    if(!renderer->program||scene->imageCount==0)return true;
    if(!ResolveTextures(renderer,scene))return false;
    if(scene->imageDirty)Rebuild(renderer,scene);
    glUseProgram(renderer->program);
    glUniform2f(renderer->viewportLocation,(float)width,(float)height);
    glUniform1i(renderer->textureLocation,0);
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(ImageVertex),(void *)offsetof(ImageVertex,position));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(ImageVertex),(void *)offsetof(ImageVertex,uv));
    glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    for(int i=0;i<scene->imageCount;i++){
        const MicroFxImageElement *element=&scene->image[i];
        if(!element->visible)continue;
        const Texture2D texture=renderer->textures[renderer->textureIndex[i]];
        bool opaque=!TextureFormatHasAlpha(texture.format)&&
                    (element->tint&255)==255&&element->opacity>=0.999f;
        if(opaque)glDisable(GL_BLEND);else glEnable(GL_BLEND);
        float c=cosf(element->rotation),s=sinf(element->rotation);
        glUniform4f(renderer->transformLocation,element->x,element->y,c,s);
        glUniform4f(renderer->colorLocation,
                    ((element->tint>>24)&255)/255.0f,
                    ((element->tint>>16)&255)/255.0f,
                    ((element->tint>>8)&255)/255.0f,
                    (element->tint&255)/255.0f*element->opacity);
        glBindTexture(GL_TEXTURE_2D,texture.id);
        glDrawArrays(GL_TRIANGLES,i*6,6);
    }
    glBindTexture(GL_TEXTURE_2D,0);
    glDisableVertexAttribArray(0);glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER,0);glUseProgram(0);
    return true;
}

void MicroFxImageRendererDestroy(MicroFxImageRenderer *renderer)
{
    for(int i=0;i<renderer->textureCount;i++){
        if(IsTextureValid(renderer->textures[i]))UnloadTexture(renderer->textures[i]);
    }
    if(renderer->vertexBuffer)glDeleteBuffers(1,&renderer->vertexBuffer);
    if(renderer->program)glDeleteProgram(renderer->program);
    *renderer=(MicroFxImageRenderer){0};
}
