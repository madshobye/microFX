#include "microfx/image_renderer.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    float position[2];
    float uv[2];
    float color[4];
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
        "attribute vec2 aUv;attribute vec4 aColor;uniform vec2 uViewport;"
        "varying mediump vec2 vUv;varying lowp vec4 vColor;"
        "void main(){vec2 p=vec2(aPosition.x/uViewport.x*2.0-1.0,"
        "1.0-aPosition.y/uViewport.y*2.0);gl_Position=vec4(p,0.0,1.0);"
        "vUv=aUv;vColor=aColor;}\n";
    static const char *fs=
        "#version 100\nprecision lowp float;varying mediump vec2 vUv;"
        "varying lowp vec4 vColor;uniform sampler2D uTexture;"
        "void main(){gl_FragColor=texture2D(uTexture,vUv)*vColor;}\n";
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
    glBindAttribLocation(renderer->program,2,"aColor");
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
            found=renderer->textureCount++;
            renderer->textures[found]=LoadTextureFromImage(image);
            UnloadImage(image);
            if(!IsTextureValid(renderer->textures[found])){
                fprintf(stderr,"MICROFX_IMAGE failed to upload texture: %s\n",path);
                return false;
            }
            snprintf(renderer->texturePaths[found],MICROFX_MAX_ASSET_PATH,"%s",path);
        }
        renderer->textureIndex[i]=found;
    }
    return true;
}

static void SetVertex(ImageVertex *vertex,const MicroFxImageElement *element,
                      float localX,float localY,float u,float v)
{
    float c=cosf(element->rotation),s=sinf(element->rotation);
    vertex->position[0]=element->x+c*localX-s*localY;
    vertex->position[1]=element->y+s*localX+c*localY;
    vertex->uv[0]=u;vertex->uv[1]=v;
    vertex->color[0]=((element->tint>>24)&255)/255.0f;
    vertex->color[1]=((element->tint>>16)&255)/255.0f;
    vertex->color[2]=((element->tint>>8)&255)/255.0f;
    vertex->color[3]=(element->tint&255)/255.0f*element->opacity;
}

static void Rebuild(MicroFxImageRenderer *renderer,MicroFxScene *scene)
{
    ImageVertex vertices[MICROFX_MAX_IMAGE_ELEMENTS*6];
    for(int i=0;i<scene->imageCount;i++){
        const MicroFxImageElement *e=&scene->image[i];
        float left=-e->width*0.5f,right=e->width*0.5f;
        float top=-e->height*0.5f,bottom=e->height*0.5f;
        ImageVertex *v=&vertices[i*6];
        SetVertex(&v[0],e,left,top,0,0);SetVertex(&v[1],e,right,top,1,0);
        SetVertex(&v[2],e,right,bottom,1,1);SetVertex(&v[3],e,left,top,0,0);
        SetVertex(&v[4],e,right,bottom,1,1);SetVertex(&v[5],e,left,bottom,0,1);
    }
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(scene->imageCount*6*sizeof(*vertices)),
                 vertices,GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    scene->imageDirty=false;
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
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(ImageVertex),(void *)offsetof(ImageVertex,position));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(ImageVertex),(void *)offsetof(ImageVertex,uv));
    glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(ImageVertex),(void *)offsetof(ImageVertex,color));
    glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    for(int i=0;i<scene->imageCount;i++){
        if(!scene->image[i].visible)continue;
        glBindTexture(GL_TEXTURE_2D,renderer->textures[renderer->textureIndex[i]].id);
        glDrawArrays(GL_TRIANGLES,i*6,6);
    }
    glBindTexture(GL_TEXTURE_2D,0);
    glDisableVertexAttribArray(0);glDisableVertexAttribArray(1);glDisableVertexAttribArray(2);
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
