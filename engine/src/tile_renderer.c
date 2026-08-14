#include "microfx/tile_renderer.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float position[2]; float uv[2]; } TileVertex;

static GLuint Compile(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,NULL);
    glCompileShader(shader);GLint ok=GL_FALSE;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[1024]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        fprintf(stderr,"MICROFX_TILE shader failure: %s\n",log);
        glDeleteShader(shader);return 0;}
    return shader;
}

bool MicroFxTileRendererInit(MicroFxTileRenderer *renderer)
{
    *renderer=(MicroFxTileRenderer){0};
    static const char *vs=
        "#version 100\nprecision highp float;attribute vec2 aPosition;"
        "attribute vec2 aUv;varying mediump vec2 vUv;"
        "void main(){gl_Position=vec4(aPosition,0.0,1.0);vUv=aUv;}\n";
    static const char *fs=
        "#version 100\nprecision mediump float;varying mediump vec2 vUv;"
        "uniform sampler2D uTexture;uniform float uGrayscale;"
        "uniform float uContrast;uniform float uBrightness;uniform float uInvert;"
        "uniform vec3 uTint;void main(){vec3 c=texture2D(uTexture,vUv).rgb;"
        "float l=dot(c,vec3(0.299,0.587,0.114));c=mix(c,vec3(l),uGrayscale);"
        "c=mix(c,vec3(1.0)-c,uInvert);c=(c-0.5)*uContrast+0.5;"
        "c=clamp(c*uBrightness*uTint,0.0,1.0);gl_FragColor=vec4(c,1.0);}\n";
    GLuint vertex=Compile(GL_VERTEX_SHADER,vs),fragment=Compile(GL_FRAGMENT_SHADER,fs);
    if(!vertex||!fragment)return false;
    renderer->program=glCreateProgram();glAttachShader(renderer->program,vertex);
    glAttachShader(renderer->program,fragment);
    glBindAttribLocation(renderer->program,0,"aPosition");
    glBindAttribLocation(renderer->program,1,"aUv");glLinkProgram(renderer->program);
    glDeleteShader(vertex);glDeleteShader(fragment);GLint linked=GL_FALSE;
    glGetProgramiv(renderer->program,GL_LINK_STATUS,&linked);if(!linked)return false;
    renderer->textureLocation=glGetUniformLocation(renderer->program,"uTexture");
    renderer->grayscaleLocation=glGetUniformLocation(renderer->program,"uGrayscale");
    renderer->contrastLocation=glGetUniformLocation(renderer->program,"uContrast");
    renderer->brightnessLocation=glGetUniformLocation(renderer->program,"uBrightness");
    renderer->invertLocation=glGetUniformLocation(renderer->program,"uInvert");
    renderer->tintLocation=glGetUniformLocation(renderer->program,"uTint");
    const TileVertex vertices[6]={
        {{-1,1},{0,0}},{{1,1},{1,0}},{{1,-1},{1,1}},
        {{-1,1},{0,0}},{{1,-1},{1,1}},{{-1,-1},{0,1}}
    };
    glGenBuffers(1,&renderer->vertexBuffer);glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,0);return renderer->vertexBuffer!=0;
}

bool MicroFxTileRendererUpdate(MicroFxTileRenderer *renderer,MicroFxScene *scene)
{
    int processed=0;
    for(int mapIndex=0;mapIndex<scene->tileMapCount;mapIndex++){
        MicroFxTileMap *map=&scene->tileMap[mapIndex];
        MicroFxTileMapRenderState *state=&renderer->maps[mapIndex];
        if(map->generation<=0)continue;
        if(state->generation!=map->generation){
            if(IsImageValid(state->staging))UnloadImage(state->staging);
            state->staging=GenImageColor(MICROFX_DESIGN_WIDTH,MICROFX_DESIGN_HEIGHT,
                                         (Color){8,12,29,255});
            if(!IsImageValid(state->staging))return false;
            state->generation=map->generation;state->decodedCount=0;
        }
        for(int i=0;i<map->tileCount&&processed<4;i++){
            MicroFxTileMapTile *tile=&map->tiles[i];
            if(!tile->received||tile->consumed)continue;
            Image image=LoadImageFromMemory(".png",tile->encoded,(int)tile->encodedSize);
            if(IsImageValid(image)){
                Rectangle source={0,0,(float)image.width,(float)image.height};
                float left=floorf(tile->x),top=floorf(tile->y);
                float right=ceilf(tile->x+tile->size),bottom=ceilf(tile->y+tile->size);
                Rectangle target={left,top,right-left,bottom-top};
                ImageDraw(&state->staging,image,source,target,WHITE);UnloadImage(image);
            }else fprintf(stderr,"MICROFX_TILE ignored invalid PNG tile %d/%d\n",
                          i,map->tileCount);
            free(tile->encoded);tile->encoded=NULL;tile->encodedSize=0;
            tile->consumed=true;state->decodedCount++;processed++;
        }
        if(state->decodedCount==map->tileCount&&IsImageValid(state->staging)){
            ImageFormat(&state->staging,PIXELFORMAT_UNCOMPRESSED_R5G6B5);
            Texture2D next=LoadTextureFromImage(state->staging);UnloadImage(state->staging);
            state->staging=(Image){0};
            if(!IsTextureValid(next))return false;
            glBindTexture(GL_TEXTURE_2D,next.id);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D,0);
            if(IsTextureValid(state->active))UnloadTexture(state->active);
            state->active=next;state->decodedCount=-1;
            map->readyGeneration=map->generation;
            printf("MICROFX_TILE ready map=%d generation=%d tiles=%d\n",
                   mapIndex,map->generation,map->tileCount);fflush(stdout);
        }
    }
    return true;
}

void MicroFxTileRendererDraw(MicroFxTileRenderer *renderer,const MicroFxScene *scene)
{
    if(!renderer->program)return;
    glUseProgram(renderer->program);glUniform1i(renderer->textureLocation,0);
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(TileVertex),(void *)offsetof(TileVertex,position));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(TileVertex),(void *)offsetof(TileVertex,uv));
    glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    for(int i=0;i<scene->tileMapCount;i++){
        const MicroFxTileMap *map=&scene->tileMap[i];
        Texture2D texture=renderer->maps[i].active;
        if(!map->visible||!IsTextureValid(texture))continue;
        glUniform1f(renderer->grayscaleLocation,map->grayscale);
        glUniform1f(renderer->contrastLocation,map->contrast);
        glUniform1f(renderer->brightnessLocation,map->brightness);
        glUniform1f(renderer->invertLocation,map->invert);
        glUniform3f(renderer->tintLocation,((map->tint>>24)&255)/255.0f,
                    ((map->tint>>16)&255)/255.0f,((map->tint>>8)&255)/255.0f);
        glBindTexture(GL_TEXTURE_2D,texture.id);glDrawArrays(GL_TRIANGLES,0,6);
    }
    glBindTexture(GL_TEXTURE_2D,0);glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);glBindBuffer(GL_ARRAY_BUFFER,0);glUseProgram(0);
}

void MicroFxTileRendererDestroy(MicroFxTileRenderer *renderer,MicroFxScene *scene)
{
    for(int i=0;i<MICROFX_MAX_TILE_MAPS;i++){
        if(IsImageValid(renderer->maps[i].staging))UnloadImage(renderer->maps[i].staging);
        if(IsTextureValid(renderer->maps[i].active))UnloadTexture(renderer->maps[i].active);
    }
    for(int i=0;i<scene->tileMapCount;i++)for(int j=0;j<MICROFX_MAX_TILE_MAP_TILES;j++){
        free(scene->tileMap[i].tiles[j].encoded);scene->tileMap[i].tiles[j].encoded=NULL;
    }
    if(renderer->vertexBuffer)glDeleteBuffers(1,&renderer->vertexBuffer);
    if(renderer->program)glDeleteProgram(renderer->program);
    *renderer=(MicroFxTileRenderer){0};
}
