#include "microfx/outline_renderer.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>

typedef struct { float position[2]; float color[4]; } OutlineVertex;
#define MAX_OUTLINE_VERTICES \
    (MICROFX_MAX_OUTLINE_ELEMENTS*MICROFX_MAX_OUTLINE_POINTS*6)

static OutlineVertex vertices[MAX_OUTLINE_VERTICES];

static GLuint Compile(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,NULL);
    glCompileShader(shader);GLint ok=GL_FALSE;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[1024]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        fprintf(stderr,"MICROFX_OUTLINE shader failure: %s\n",log);
        glDeleteShader(shader);return 0;}
    return shader;
}

bool MicroFxOutlineRendererInit(MicroFxOutlineRenderer *renderer)
{
    *renderer=(MicroFxOutlineRenderer){0};
    static const char *vs=
        "#version 100\nprecision highp float;attribute vec2 aPosition;"
        "attribute vec4 aColor;uniform vec2 uViewport;varying lowp vec4 vColor;"
        "void main(){vec2 p=vec2(aPosition.x/uViewport.x*2.0-1.0,"
        "1.0-aPosition.y/uViewport.y*2.0);gl_Position=vec4(p,0.0,1.0);"
        "vColor=aColor;}\n";
    static const char *fs=
        "#version 100\nprecision lowp float;varying lowp vec4 vColor;"
        "void main(){gl_FragColor=vColor;}\n";
    GLuint vertex=Compile(GL_VERTEX_SHADER,vs),fragment=Compile(GL_FRAGMENT_SHADER,fs);
    if(!vertex||!fragment)return false;
    renderer->program=glCreateProgram();glAttachShader(renderer->program,vertex);
    glAttachShader(renderer->program,fragment);
    glBindAttribLocation(renderer->program,0,"aPosition");
    glBindAttribLocation(renderer->program,1,"aColor");glLinkProgram(renderer->program);
    glDeleteShader(vertex);glDeleteShader(fragment);GLint linked=GL_FALSE;
    glGetProgramiv(renderer->program,GL_LINK_STATUS,&linked);if(!linked)return false;
    renderer->viewportLocation=glGetUniformLocation(renderer->program,"uViewport");
    glGenBuffers(3,renderer->vertexBuffers);
    return renderer->vertexBuffers[0]&&renderer->vertexBuffers[1]&&
           renderer->vertexBuffers[2];
}

static void Color(float out[4],uint32_t color,float opacity)
{
    out[0]=((color>>24)&255)/255.0f;out[1]=((color>>16)&255)/255.0f;
    out[2]=((color>>8)&255)/255.0f;out[3]=(color&255)/255.0f*opacity;
}

static void Vertex(OutlineVertex *out,float x,float y,const float color[4])
{
    out->position[0]=x;out->position[1]=y;
    for(int i=0;i<4;i++)out->color[i]=color[i];
}

static void Point(const MicroFxOutlineElement *element,int index,float *x,float *y)
{
    float px=element->points[index][0]*element->scale;
    float py=element->points[index][1]*element->scale;
    float c=cosf(element->rotation),s=sinf(element->rotation);
    *x=element->x+c*px-s*py;*y=element->y+s*px+c*py;
}

static float Cross(float ax,float ay,float bx,float by,float cx,float cy)
{
    return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
}

static bool InsideTriangle(float px,float py,float ax,float ay,float bx,float by,
                           float cx,float cy,float orientation)
{
    return Cross(ax,ay,bx,by,px,py)*orientation>=-0.0001f&&
           Cross(bx,by,cx,cy,px,py)*orientation>=-0.0001f&&
           Cross(cx,cy,ax,ay,px,py)*orientation>=-0.0001f;
}

static int FilledVertices(const MicroFxOutlineElement *element,int cursor,
                          const float color[4])
{
    float x[MICROFX_MAX_OUTLINE_POINTS],y[MICROFX_MAX_OUTLINE_POINTS];
    int indices[MICROFX_MAX_OUTLINE_POINTS];
    for(int i=0;i<element->pointCount;i++){
        Point(element,i,&x[i],&y[i]);indices[i]=i;
    }
    float area=0.0f;
    for(int i=0;i<element->pointCount;i++){
        int next=(i+1)%element->pointCount;
        area+=x[i]*y[next]-x[next]*y[i];
    }
    const float orientation=area>=0.0f?1.0f:-1.0f;
    int remaining=element->pointCount;
    while(remaining>2){
        bool found=false;
        for(int ear=0;ear<remaining;ear++){
            int a=indices[(ear+remaining-1)%remaining];
            int b=indices[ear];
            int c=indices[(ear+1)%remaining];
            if(Cross(x[a],y[a],x[b],y[b],x[c],y[c])*orientation<=0.0001f)
                continue;
            bool contains=false;
            for(int other=0;other<remaining;other++){
                int p=indices[other];
                if(p==a||p==b||p==c)continue;
                if(InsideTriangle(x[p],y[p],x[a],y[a],x[b],y[b],x[c],y[c],
                                  orientation)){contains=true;break;}
            }
            if(contains)continue;
            Vertex(&vertices[cursor++],x[a],y[a],color);
            Vertex(&vertices[cursor++],x[b],y[b],color);
            Vertex(&vertices[cursor++],x[c],y[c],color);
            for(int move=ear;move<remaining-1;move++)indices[move]=indices[move+1];
            remaining--;found=true;break;
        }
        if(!found)break;
    }
    return cursor;
}

static void Rebuild(MicroFxOutlineRenderer *renderer,MicroFxScene *scene)
{
    int cursor=0;
    for(int i=0;i<scene->outlineCount;i++){
        const MicroFxOutlineElement *element=&scene->outline[i];
        if(!element->visible)continue;
        float color[4];Color(color,element->color,element->opacity);
        if(element->filled){cursor=FilledVertices(element,cursor,color);continue;}
        int segments=element->closed?element->pointCount:element->pointCount-1;
        for(int segment=0;segment<segments;segment++){
            float ax,ay,bx,by;Point(element,segment,&ax,&ay);
            Point(element,(segment+1)%element->pointCount,&bx,&by);
            float dx=bx-ax,dy=by-ay,length=sqrtf(dx*dx+dy*dy);
            if(length<0.0001f)continue;
            float half=element->width*0.5f;
            float tx=dx/length*half,ty=dy/length*half;
            float nx=-dy/length*half,ny=dx/length*half;
            ax-=tx;ay-=ty;bx+=tx;by+=ty;
            Vertex(&vertices[cursor++],ax+nx,ay+ny,color);
            Vertex(&vertices[cursor++],bx+nx,by+ny,color);
            Vertex(&vertices[cursor++],bx-nx,by-ny,color);
            Vertex(&vertices[cursor++],ax+nx,ay+ny,color);
            Vertex(&vertices[cursor++],bx-nx,by-ny,color);
            Vertex(&vertices[cursor++],ax-nx,ay-ny,color);
        }
    }
    renderer->activeVertexBuffer=(renderer->activeVertexBuffer+1u)%3u;
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffers[renderer->activeVertexBuffer]);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(cursor*sizeof(vertices[0])),
                 vertices,GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,0);renderer->vertexCount=cursor;
    scene->outlineDirty=false;
}

void MicroFxOutlineRendererDraw(MicroFxOutlineRenderer *renderer,
                                MicroFxScene *scene,int width,int height)
{
    if(!renderer->program||scene->outlineCount==0)return;
    if(scene->outlineDirty)Rebuild(renderer,scene);
    if(renderer->vertexCount==0)return;
    glUseProgram(renderer->program);
    glUniform2f(renderer->viewportLocation,(float)width,(float)height);
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffers[renderer->activeVertexBuffer]);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(OutlineVertex),
                          (void *)offsetof(OutlineVertex,position));
    glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(OutlineVertex),
                          (void *)offsetof(OutlineVertex,color));
    glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES,0,renderer->vertexCount);
    glDisableVertexAttribArray(0);glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER,0);glUseProgram(0);
}

void MicroFxOutlineRendererDestroy(MicroFxOutlineRenderer *renderer)
{
    glDeleteBuffers(3,renderer->vertexBuffers);
    if(renderer->program)glDeleteProgram(renderer->program);
    *renderer=(MicroFxOutlineRenderer){0};
}
