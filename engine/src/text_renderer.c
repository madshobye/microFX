#include "microfx/text_renderer.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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
    *renderer=(MicroFxTextRenderer){0}; renderer->font=font;
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

static void Rebuild(MicroFxTextRenderer *renderer,MicroFxScene *scene)
{
    int glyphCapacity=MICROFX_MAX_TEXT_ELEMENTS*(MICROFX_MAX_TEXT_BYTES-1);
    TextVertex *vertices=malloc((size_t)glyphCapacity*6*sizeof(*vertices));
    if (!vertices){renderer->vertexCount=0;return;}
    int cursor=0;
    for(int t=0;t<scene->textCount;t++){
        const MicroFxTextElement *e=&scene->text[t];
        if(!e->visible)continue;
        float scale=e->size/renderer->font.baseSize;
        float originX=e->x,x=originX,y=e->y,color[4];DecodeColor(color,e->color);
        for(const unsigned char *p=(const unsigned char *)e->text;*p;p++){
            if(*p=='\n'){x=originX;y+=e->size*1.25f;continue;}
            int glyph=GetGlyphIndex(renderer->font,*p);
            Rectangle rec=renderer->font.recs[glyph];GlyphInfo info=renderer->font.glyphs[glyph];
            float x0=x+info.offsetX*scale,y0=y+info.offsetY*scale;
            float x1=x0+rec.width*scale,y1=y0+rec.height*scale;
            float u0=rec.x/renderer->font.texture.width,v0=rec.y/renderer->font.texture.height;
            float u1=(rec.x+rec.width)/renderer->font.texture.width;
            float v1=(rec.y+rec.height)/renderer->font.texture.height;
            TextVertex *q=&vertices[cursor];
            Vertex(&q[0],x0,y0,u0,v0,color);Vertex(&q[1],x1,y0,u1,v0,color);
            Vertex(&q[2],x1,y1,u1,v1,color);Vertex(&q[3],x0,y0,u0,v0,color);
            Vertex(&q[4],x1,y1,u1,v1,color);Vertex(&q[5],x0,y1,u0,v1,color);cursor+=6;
            x+=(info.advanceX?info.advanceX:rec.width)*scale+e->size*.08f;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(cursor*sizeof(*vertices)),vertices,GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,0);renderer->vertexCount=cursor;scene->textDirty=false;free(vertices);
}

void MicroFxTextRendererDraw(MicroFxTextRenderer *renderer,MicroFxScene *scene,
                            int width,int height)
{
    if(!renderer->program||scene->textCount==0)return;
    if(scene->textDirty)Rebuild(renderer,scene);
    glUseProgram(renderer->program);glUniform2f(renderer->viewportLocation,(float)width,(float)height);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,renderer->font.texture.id);
    glUniform1i(renderer->textureLocation,0);glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    for(int i=0;i<3;i++)glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(TextVertex),(void *)offsetof(TextVertex,position));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(TextVertex),(void *)offsetof(TextVertex,uv));
    glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(TextVertex),(void *)offsetof(TextVertex,color));
    // Establish all raster state used by this overlay. raylib's preceding 3D
    // pass may leave culling and depth writes enabled on the DRM/GLES backend.
    glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES,0,renderer->vertexCount);
    for(int i=0;i<3;i++)glDisableVertexAttribArray((GLuint)i);
    glBindBuffer(GL_ARRAY_BUFFER,0);glBindTexture(GL_TEXTURE_2D,0);glUseProgram(0);
}

void MicroFxTextRendererDestroy(MicroFxTextRenderer *renderer)
{
    if(renderer->vertexBuffer)glDeleteBuffers(1,&renderer->vertexBuffer);
    if(renderer->program)glDeleteProgram(renderer->program);
    *renderer=(MicroFxTextRenderer){0};
}
