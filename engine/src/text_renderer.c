#include "microfx/text_renderer.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float position[2];
    float uv[2];
    float color[4];
} TextVertex;

static GLuint Compile(GLenum type, const char *source)
{
    GLuint shader=glCreateShader(type);
    glShaderSource(shader,1,&source,NULL); glCompileShader(shader);
    GLint ok=GL_FALSE; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if (!ok) {
        char log[1024]={0}; glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        fprintf(stderr,"MICROFX_TEXT shader failure: %s\n",log);
        glDeleteShader(shader); return 0;
    }
    return shader;
}

bool MicroFxTextRendererInit(MicroFxTextRenderer *renderer, Font font)
{
    *renderer=(MicroFxTextRenderer){0};
    renderer->fonts[0]=font;renderer->fontCount=1;
    static const char *vs=
        "#version 100\nprecision highp float;attribute vec2 aPosition;attribute vec2 aUv;"
        "attribute vec4 aColor;uniform vec2 uViewport;varying vec2 vUv;varying lowp vec4 vColor;"
        "void main(){vec2 p=vec2(aPosition.x/uViewport.x*2.0-1.0,1.0-aPosition.y/uViewport.y*2.0);"
        "gl_Position=vec4(p,0.0,1.0);vUv=aUv;vColor=aColor;}\n";
    static const char *fs=
        "#version 100\nprecision mediump float;uniform sampler2D uTexture;"
        "varying vec2 vUv;varying lowp vec4 vColor;"
        "void main(){float a=texture2D(uTexture,vUv).a;gl_FragColor=vec4(vColor.rgb,vColor.a*a);}\n";
    GLuint vertex=Compile(GL_VERTEX_SHADER,vs),fragment=Compile(GL_FRAGMENT_SHADER,fs);
    if (!vertex || !fragment) return false;
    renderer->program=glCreateProgram(); glAttachShader(renderer->program,vertex);
    glAttachShader(renderer->program,fragment);
    glBindAttribLocation(renderer->program,0,"aPosition");
    glBindAttribLocation(renderer->program,1,"aUv");
    glBindAttribLocation(renderer->program,2,"aColor");
    glLinkProgram(renderer->program); glDeleteShader(vertex); glDeleteShader(fragment);
    GLint linked=GL_FALSE; glGetProgramiv(renderer->program,GL_LINK_STATUS,&linked);
    if (!linked) return false;
    renderer->viewportLocation=glGetUniformLocation(renderer->program,"uViewport");
    renderer->textureLocation=glGetUniformLocation(renderer->program,"uTexture");
    glGenBuffers(1,&renderer->vertexBuffer);
    return renderer->vertexBuffer!=0;
}

static void DecodeColor(float *out,uint32_t color)
{
    out[0]=((color>>24)&255)/255.0f;out[1]=((color>>16)&255)/255.0f;
    out[2]=((color>>8)&255)/255.0f;out[3]=(color&255)/255.0f;
}

static void Vertex(TextVertex *out,float x,float y,float u,float v,const float *color)
{
    out->position[0]=x;out->position[1]=y;out->uv[0]=u;out->uv[1]=v;
    for(int i=0;i<4;i++)out->color[i]=color[i];
}

static void RotatedVertex(TextVertex *out,float x,float y,float originX,float originY,
                          float cosine,float sine,float u,float v,const float *color)
{
    float dx=x-originX,dy=y-originY;
    Vertex(out,originX+cosine*dx-sine*dy,originY+sine*dx+cosine*dy,u,v,color);
}

static bool ResolveFonts(MicroFxTextRenderer *renderer,MicroFxScene *scene)
{
    for(int i=0;i<scene->textCount;i++){
        const char *path=scene->text[i].fontPath;
        if(!path[0]){renderer->fontIndex[i]=0;continue;}
        int found=-1;
        for(int j=1;j<renderer->fontCount;j++){
            if(strcmp(renderer->fontPaths[j],path)==0){found=j;break;}
        }
        if(found<0){
            if(renderer->fontCount>=MICROFX_MAX_FONT_FACES){
                fprintf(stderr,"MICROFX_TEXT too many font faces (maximum %d): %s\n",
                        MICROFX_MAX_FONT_FACES,path);
                return false;
            }
            Font loaded=LoadFontEx(path,64,NULL,0);
            if(loaded.texture.id==0||loaded.glyphCount<=0||
               loaded.texture.id==renderer->fonts[0].texture.id){
                fprintf(stderr,"MICROFX_TEXT failed to load font asset: %s\n",path);
                return false;
            }
            found=renderer->fontCount++;
            renderer->fonts[found]=loaded;renderer->fontOwned[found]=true;
            snprintf(renderer->fontPaths[found],MICROFX_MAX_ASSET_PATH,"%s",path);
        }
        renderer->fontIndex[i]=found;
    }
    return true;
}

static bool Rebuild(MicroFxTextRenderer *renderer,MicroFxScene *scene)
{
    int glyphCapacity=MICROFX_MAX_TEXT_ELEMENTS*(MICROFX_MAX_TEXT_BYTES-1);
    TextVertex *vertices=malloc((size_t)glyphCapacity*6*sizeof(*vertices));
    if (!vertices){renderer->vertexCount=0;return false;}
    int cursor=0;
    for(int t=0;t<scene->textCount;t++){
        const MicroFxTextElement *e=&scene->text[t];
        renderer->firstVertex[t]=cursor;renderer->vertexCounts[t]=0;
        if(!e->visible)continue;
        Font font=renderer->fonts[renderer->fontIndex[t]];
        float scale=e->size/font.baseSize;
        float originX=e->x,x=originX,y=e->y,color[4];DecodeColor(color,e->color);
        float cosine=cosf(e->rotation),sine=sinf(e->rotation);
        color[3]*=e->opacity;
        for(const unsigned char *p=(const unsigned char *)e->text;*p;p++){
            if(*p=='\n'){x=originX;y+=e->size*1.25f;continue;}
            int glyph=GetGlyphIndex(font,*p);
            Rectangle rec=font.recs[glyph];GlyphInfo info=font.glyphs[glyph];
            float x0=x+info.offsetX*scale,y0=y+info.offsetY*scale;
            float x1=x0+rec.width*scale,y1=y0+rec.height*scale;
            float u0=rec.x/font.texture.width,v0=rec.y/font.texture.height;
            float u1=(rec.x+rec.width)/font.texture.width;
            float v1=(rec.y+rec.height)/font.texture.height;
            TextVertex *q=&vertices[cursor];
            RotatedVertex(&q[0],x0,y0,originX,e->y,cosine,sine,u0,v0,color);
            RotatedVertex(&q[1],x1,y0,originX,e->y,cosine,sine,u1,v0,color);
            RotatedVertex(&q[2],x1,y1,originX,e->y,cosine,sine,u1,v1,color);
            RotatedVertex(&q[3],x0,y0,originX,e->y,cosine,sine,u0,v0,color);
            RotatedVertex(&q[4],x1,y1,originX,e->y,cosine,sine,u1,v1,color);
            RotatedVertex(&q[5],x0,y1,originX,e->y,cosine,sine,u0,v1,color);cursor+=6;
            x+=(info.advanceX?info.advanceX:rec.width)*scale+e->size*.08f;
        }
        renderer->vertexCounts[t]=cursor-renderer->firstVertex[t];
    }
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(cursor*sizeof(*vertices)),vertices,GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,0);renderer->vertexCount=cursor;scene->textDirty=false;free(vertices);
    return true;
}

bool MicroFxTextRendererDraw(MicroFxTextRenderer *renderer,MicroFxScene *scene,
                             int width,int height)
{
    if(!renderer->program||scene->textCount==0)return true;
    if(!ResolveFonts(renderer,scene))return false;
    if(scene->textDirty&&!Rebuild(renderer,scene))return false;
    glUseProgram(renderer->program);glUniform2f(renderer->viewportLocation,(float)width,(float)height);
    glActiveTexture(GL_TEXTURE0);glUniform1i(renderer->textureLocation,0);
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    for(int i=0;i<3;i++)glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(TextVertex),(void *)offsetof(TextVertex,position));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(TextVertex),(void *)offsetof(TextVertex,uv));
    glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(TextVertex),(void *)offsetof(TextVertex,color));
    // Establish all raster state used by this overlay. raylib's preceding 3D
    // pass may leave culling and depth writes enabled on the DRM/GLES backend.
    glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    int runFirst=0,runCount=0,runFont=-1;
    for(int i=0;i<scene->textCount;i++){
        int count=renderer->vertexCounts[i];
        if(count==0)continue;
        int font=renderer->fontIndex[i];
        if(runCount>0&&(font!=runFont||renderer->firstVertex[i]!=runFirst+runCount)){
            glBindTexture(GL_TEXTURE_2D,renderer->fonts[runFont].texture.id);
            glDrawArrays(GL_TRIANGLES,runFirst,runCount);runCount=0;
        }
        if(runCount==0){runFirst=renderer->firstVertex[i];runFont=font;}
        runCount+=count;
    }
    if(runCount>0){
        glBindTexture(GL_TEXTURE_2D,renderer->fonts[runFont].texture.id);
        glDrawArrays(GL_TRIANGLES,runFirst,runCount);
    }
    for(int i=0;i<3;i++)glDisableVertexAttribArray((GLuint)i);
    glBindBuffer(GL_ARRAY_BUFFER,0);glBindTexture(GL_TEXTURE_2D,0);glUseProgram(0);
    return true;
}

void MicroFxTextRendererDestroy(MicroFxTextRenderer *renderer)
{
    for(int i=1;i<renderer->fontCount;i++)if(renderer->fontOwned[i])UnloadFont(renderer->fonts[i]);
    if(renderer->vertexBuffer)glDeleteBuffers(1,&renderer->vertexBuffer);
    if(renderer->program)glDeleteProgram(renderer->program);
    *renderer=(MicroFxTextRenderer){0};
}
