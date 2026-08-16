#include "microfx/gpu_texture_renderer.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { float position[2]; float uv[2]; } GpuTextureVertex;

static const char *VertexSource=
    "#version 100\nprecision highp float;attribute vec2 aPosition;"
    "attribute vec2 aUv;varying mediump vec2 vUv;"
    "void main(){gl_Position=vec4(aPosition,0.0,1.0);vUv=aUv;}\n";

static GLuint Compile(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,NULL);
    glCompileShader(shader);GLint ok=GL_FALSE;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){
        char log[1024]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        fprintf(stderr,"MICROFX_GPU_TEXTURE shader failure: %s\n",log);
        glDeleteShader(shader);return 0;
    }
    return shader;
}

static GLuint LinkProgram(const char *fragmentSource)
{
    GLuint vertex=Compile(GL_VERTEX_SHADER,VertexSource);
    GLuint fragment=Compile(GL_FRAGMENT_SHADER,fragmentSource);
    if(!vertex||!fragment){
        if(vertex)glDeleteShader(vertex);if(fragment)glDeleteShader(fragment);
        return 0;
    }
    GLuint program=glCreateProgram();glAttachShader(program,vertex);
    glAttachShader(program,fragment);glBindAttribLocation(program,0,"aPosition");
    glBindAttribLocation(program,1,"aUv");glLinkProgram(program);
    glDeleteShader(vertex);glDeleteShader(fragment);GLint linked=GL_FALSE;
    glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked){
        char log[1024]={0};glGetProgramInfoLog(program,sizeof(log),NULL,log);
        fprintf(stderr,"MICROFX_GPU_TEXTURE shader link failure: %s\n",log);
        glDeleteProgram(program);return 0;
    }
    return program;
}

bool MicroFxGpuTextureRendererInit(MicroFxGpuTextureRenderer *renderer)
{
    *renderer=(MicroFxGpuTextureRenderer){0};
    static const char *fragment=
        "#version 100\nprecision mediump float;varying mediump vec2 vUv;"
        "uniform sampler2D uTexture;"
        "void main(){gl_FragColor=texture2D(uTexture,vUv);}\n";
    static const char *cachedFragment=
        "#version 100\nprecision mediump float;varying mediump vec2 vUv;"
        "uniform sampler2D uTexture;uniform lowp float uOpacity;"
        "void main(){lowp vec4 c=texture2D(uTexture,vec2(vUv.x,1.0-vUv.y));"
        "gl_FragColor=vec4(c.rgb,c.a*uOpacity);}\n";
    renderer->defaultProgram=LinkProgram(fragment);
    renderer->cachedProgram=LinkProgram(cachedFragment);
    if(!renderer->defaultProgram||!renderer->cachedProgram)return false;
    renderer->defaultTextureLocation=
        glGetUniformLocation(renderer->defaultProgram,"uTexture");
    renderer->cachedTextureLocation=
        glGetUniformLocation(renderer->cachedProgram,"uTexture");
    renderer->cachedOpacityLocation=
        glGetUniformLocation(renderer->cachedProgram,"uOpacity");
    const GpuTextureVertex vertices[6]={
        {{-1,1},{0,0}},{{1,1},{1,0}},{{1,-1},{1,1}},
        {{-1,1},{0,0}},{{1,-1},{1,1}},{{-1,-1},{0,1}}
    };
    glGenBuffers(1,&renderer->vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    return renderer->vertexBuffer!=0;
}

static bool UpdateShader(MicroFxGpuTextureRenderState *state,
                         const MicroFxGpuTexture *texture)
{
    if(state->shaderVersion==texture->shaderVersion)return true;
    if(texture->secondary&&!texture->fragmentPath[0]){
        fprintf(stderr,
            "MICROFX_GPU_TEXTURE secondary source requires a custom shader\n");
        return false;
    }
    if(state->program){glDeleteProgram(state->program);state->program=0;}
    state->textureLocation=-1;state->secondaryTextureLocation=-1;
    state->fieldLocation=-1;
    state->fieldSizeLocation=-1;state->resolutionLocation=-1;
    state->timeLocation=-1;state->paramsLocation=-1;
    if(texture->fragmentPath[0]){
        char *source=LoadFileText(texture->fragmentPath);
        if(!source){
            fprintf(stderr,"MICROFX_GPU_TEXTURE could not read shader %s\n",
                    texture->fragmentPath);return false;
        }
        state->program=LinkProgram(source);UnloadFileText(source);
        if(!state->program)return false;
        state->textureLocation=glGetUniformLocation(state->program,"uTexture");
        state->secondaryTextureLocation=
            glGetUniformLocation(state->program,"uTexture2");
        state->fieldLocation=glGetUniformLocation(state->program,"uField");
        state->fieldSizeLocation=glGetUniformLocation(state->program,"uFieldSize");
        state->resolutionLocation=glGetUniformLocation(state->program,"uResolution");
        state->timeLocation=glGetUniformLocation(state->program,"uTime");
        state->paramsLocation=glGetUniformLocation(state->program,"uParams[0]");
        if(state->textureLocation<0){
            fprintf(stderr,
                "MICROFX_GPU_TEXTURE shader contract missing uTexture: %s\n",
                texture->fragmentPath);return false;
        }
        if(texture->secondary&&state->secondaryTextureLocation<0){
            fprintf(stderr,
                "MICROFX_GPU_TEXTURE secondary source requires uTexture2: %s\n",
                texture->fragmentPath);return false;
        }
        printf("MICROFX_GPU_TEXTURE shader loaded fragment=%s\n",
               texture->fragmentPath);fflush(stdout);
    }
    state->shaderVersion=texture->shaderVersion;
    return true;
}

static bool UpdateField(MicroFxGpuTextureRenderState *state,
                        const MicroFxGpuTexture *texture)
{
    if(state->fieldVersion==texture->fieldVersion)return true;
    if(!texture->fieldRgba||texture->fieldWidth<=0||texture->fieldHeight<=0)
        return false;
    if(!state->fieldTexture)glGenTextures(1,&state->fieldTexture);
    if(!state->fieldTexture)return false;
    glBindTexture(GL_TEXTURE_2D,state->fieldTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,texture->fieldWidth,
                 texture->fieldHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,
                 texture->fieldRgba);
    // Data fields represent sparse control values, not display pixels. Linear
    // sampling turns a coarse weather/simulation field into smooth parameters
    // without four manual texture reads in every fragment shader.
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D,0);state->fieldVersion=texture->fieldVersion;
    return true;
}

static bool UpdateAsset(MicroFxGpuTextureRenderState *state,
                        const MicroFxGpuTexture *texture)
{
    if(texture->source==MICROFX_GPU_TEXTURE_ASSET&&!state->assetLoaded){
        Image image=LoadImage(texture->assetPath);
        if(!IsImageValid(image)){
            fprintf(stderr,"MICROFX_GPU_TEXTURE failed to load asset: %s\n",
                    texture->assetPath);return false;
        }
        state->assetTexture=LoadTextureFromImage(image);UnloadImage(image);
        if(!IsTextureValid(state->assetTexture))return false;
        glBindTexture(GL_TEXTURE_2D,state->assetTexture.id);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D,0);state->assetLoaded=true;
    }
    if(state->secondaryVersion==texture->secondaryVersion)return true;
    if(IsTextureValid(state->secondaryAssetTexture))
        UnloadTexture(state->secondaryAssetTexture);
    state->secondaryAssetTexture=(Texture2D){0};
    if(texture->secondary&&
       texture->secondarySource==MICROFX_GPU_TEXTURE_ASSET){
        Image image=LoadImage(texture->secondaryAssetPath);
        if(!IsImageValid(image)){
            fprintf(stderr,"MICROFX_GPU_TEXTURE failed to load secondary asset: %s\n",
                    texture->secondaryAssetPath);return false;
        }
        state->secondaryAssetTexture=LoadTextureFromImage(image);UnloadImage(image);
        if(!IsTextureValid(state->secondaryAssetTexture))return false;
        glBindTexture(GL_TEXTURE_2D,state->secondaryAssetTexture.id);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D,0);
    }
    state->secondaryVersion=texture->secondaryVersion;return true;
}

bool MicroFxGpuTextureRendererUpdate(MicroFxGpuTextureRenderer *renderer,
                                     MicroFxScene *scene)
{
    for(int i=0;i<scene->gpuTextureCount;i++){
        MicroFxGpuTextureRenderState *state=&renderer->textures[i];
        const MicroFxGpuTexture *texture=&scene->gpuTexture[i];
        if(!UpdateShader(state,texture)||!UpdateAsset(state,texture))return false;
        if(texture->fieldVersion>0&&!UpdateField(state,texture))return false;
    }
    return true;
}

static bool EnsureCache(MicroFxGpuTextureRenderState *state,int width,int height)
{
    if(state->cachedTexture&&state->cachedWidth==width&&state->cachedHeight==height)
        return true;
    if(state->cachedFramebuffer)glDeleteFramebuffers(1,&state->cachedFramebuffer);
    if(state->cachedTexture)glDeleteTextures(1,&state->cachedTexture);
    state->cachedFramebuffer=0;state->cachedTexture=0;state->cacheValid=false;
    glGenTextures(1,&state->cachedTexture);
    glBindTexture(GL_TEXTURE_2D,state->cachedTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,
                 GL_UNSIGNED_SHORT_5_6_5,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1,&state->cachedFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,state->cachedFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
                           state->cachedTexture,0);
    bool complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,0);
    if(!complete){
        fprintf(stderr,"MICROFX_GPU_TEXTURE cache framebuffer incomplete\n");
        return false;
    }
    state->cachedWidth=width;state->cachedHeight=height;return true;
}

static void DrawPass(const MicroFxGpuTextureRenderer *renderer,
                     const MicroFxGpuTextureRenderState *state,
                     const MicroFxGpuTexture *texture,Texture2D source,
                     Texture2D secondary,int width,int height,bool custom,
                     bool blend,float time)
{
    GLuint program=custom?state->program:renderer->defaultProgram;
    GLint sourceLocation=custom?state->textureLocation:
        renderer->defaultTextureLocation;
    glUseProgram(program);glUniform1i(sourceLocation,0);
    if(blend){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}
    else glDisable(GL_BLEND);
    if(custom){
        if(texture->secondary){
            glUniform1i(state->secondaryTextureLocation,2);
            glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,secondary.id);
        }
        if(state->fieldLocation>=0){
            glUniform1i(state->fieldLocation,1);glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D,state->fieldTexture);
        }
        if(state->fieldSizeLocation>=0)
            glUniform2f(state->fieldSizeLocation,(float)texture->fieldWidth,
                        (float)texture->fieldHeight);
        if(state->resolutionLocation>=0)
            glUniform2f(state->resolutionLocation,(float)width,(float)height);
        if(state->timeLocation>=0)
            glUniform1f(state->timeLocation,time);
        if(state->paramsLocation>=0)
            glUniform4fv(state->paramsLocation,
                         MICROFX_MAX_GPU_TEXTURE_PARAMS/4,texture->params);
    }
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source.id);
    glDrawArrays(GL_TRIANGLES,0,6);
}

static void DrawCachedPass(const MicroFxGpuTextureRenderer *renderer,
                           Texture2D source,bool blend,float opacity)
{
    glUseProgram(renderer->cachedProgram);
    glUniform1i(renderer->cachedTextureLocation,0);
    glUniform1f(renderer->cachedOpacityLocation,opacity);
    if(blend){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}
    else glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source.id);
    glDrawArrays(GL_TRIANGLES,0,6);
}

void MicroFxGpuTextureRendererDraw(MicroFxGpuTextureRenderer *renderer,
                                   const MicroFxTileRenderer *tiles,
                                   const MicroFxScene *scene,
                                   MicroFxGpuTextureStage stage,
                                   int width,int height)
{
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(GpuTextureVertex),
                          (void *)offsetof(GpuTextureVertex,position));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(GpuTextureVertex),
                          (void *)offsetof(GpuTextureVertex,uv));
    glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    for(int i=0;i<scene->gpuTextureCount;i++){
        const MicroFxGpuTexture *texture=&scene->gpuTexture[i];
        MicroFxGpuTextureRenderState *state=&renderer->textures[i];
        if(!texture->visible||texture->stage!=stage)continue;
        // A tile map can remain loaded as a texture source without also being
        // drawn as a layer. Source availability and layer visibility are
        // deliberately independent so an opaque shader can replace the map
        // in one full-screen pass instead of drawing the map twice.
        if(texture->source==MICROFX_GPU_TEXTURE_MAP&&
           (texture->mapIndex<0||texture->mapIndex>=scene->tileMapCount))continue;
        if(texture->secondary&&texture->secondarySource==MICROFX_GPU_TEXTURE_MAP&&
           (texture->secondaryMapIndex<0||
            texture->secondaryMapIndex>=scene->tileMapCount))continue;
        Texture2D source=texture->source==MICROFX_GPU_TEXTURE_MAP?
            MicroFxTileRendererTexture(tiles,texture->mapIndex):
            state->assetTexture;
        if(!IsTextureValid(source))continue;
        Texture2D secondary=texture->secondary?
            (texture->secondarySource==MICROFX_GPU_TEXTURE_MAP?
             MicroFxTileRendererTexture(tiles,texture->secondaryMapIndex):
             state->secondaryAssetTexture):(Texture2D){0};
        if(texture->secondary&&!IsTextureValid(secondary))continue;
        // Bake static background composition independently from final layer
        // opacity. Sun fades then blend two RGB565 caches without re-running
        // their multi-source shaders every frame.
        const bool staticShader=state->program==0||state->timeLocation<0;
        const bool cacheable=stage==MICROFX_GPU_TEXTURE_BACKGROUND&&staticShader;
        if(cacheable){
            if(!EnsureCache(state,width,height))continue;
            const bool dirty=!state->cacheValid||
                state->cachedShaderVersion!=texture->shaderVersion||
                state->cachedParamVersion!=texture->paramVersion||
                state->cachedFieldVersion!=texture->fieldVersion||
                state->cachedSourceId!=source.id||
                state->cachedSecondaryId!=secondary.id;
            if(dirty){
                GLint framebuffer=0,viewport[4]={0};
                glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
                glGetIntegerv(GL_VIEWPORT,viewport);
                glBindFramebuffer(GL_FRAMEBUFFER,state->cachedFramebuffer);
                glViewport(0,0,width,height);
                DrawPass(renderer,state,texture,source,secondary,width,height,
                         state->program!=0,false,scene->time);
                glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)framebuffer);
                glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
                state->cachedShaderVersion=texture->shaderVersion;
                state->cachedParamVersion=texture->paramVersion;
                state->cachedFieldVersion=texture->fieldVersion;
                state->cachedSourceId=source.id;
                state->cachedSecondaryId=secondary.id;
                state->cacheValid=true;
                printf("MICROFX_GPU_TEXTURE cache_bake index=%d size=%dx%d\n",
                       i,width,height);fflush(stdout);
            }
            Texture2D cached={.id=state->cachedTexture,.width=width,
                .height=height,.mipmaps=1,.format=PIXELFORMAT_UNCOMPRESSED_R5G6B5};
            DrawCachedPass(renderer,cached,texture->blend,texture->opacity);
        }else{
            DrawPass(renderer,state,texture,source,secondary,width,height,
                     state->program!=0,texture->blend,scene->time);
        }
    }
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
    glDisableVertexAttribArray(0);glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER,0);glUseProgram(0);
}

void MicroFxGpuTextureRendererDestroy(MicroFxGpuTextureRenderer *renderer,
                                      MicroFxScene *scene)
{
    for(int i=0;i<MICROFX_MAX_GPU_TEXTURES;i++){
        MicroFxGpuTextureRenderState *state=&renderer->textures[i];
        if(state->program)glDeleteProgram(state->program);
        if(state->fieldTexture)glDeleteTextures(1,&state->fieldTexture);
        if(state->cachedFramebuffer)
            glDeleteFramebuffers(1,&state->cachedFramebuffer);
        if(state->cachedTexture)glDeleteTextures(1,&state->cachedTexture);
        if(IsTextureValid(state->assetTexture))UnloadTexture(state->assetTexture);
        if(IsTextureValid(state->secondaryAssetTexture))
            UnloadTexture(state->secondaryAssetTexture);
    }
    for(int i=0;i<scene->gpuTextureCount;i++){
        free(scene->gpuTexture[i].fieldRgba);
        scene->gpuTexture[i].fieldRgba=NULL;
    }
    if(renderer->vertexBuffer)glDeleteBuffers(1,&renderer->vertexBuffer);
    if(renderer->defaultProgram)glDeleteProgram(renderer->defaultProgram);
    if(renderer->cachedProgram)glDeleteProgram(renderer->cachedProgram);
    *renderer=(MicroFxGpuTextureRenderer){0};
}
