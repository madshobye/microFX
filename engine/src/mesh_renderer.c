#include "microfx/mesh_renderer.h"
#include "microfx/gpu_math.h"
#include <raylib.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define SPHERE_RINGS 8
#define SPHERE_SLICES 12

typedef struct {
    float position[3];
    float normal[3];
    float object;
} MeshVertex;

static GLuint Compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = { 0 };
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "MICROFX_MESH shader failure: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool MicroFxMeshRendererInit(MicroFxMeshRenderer *renderer)
{
    *renderer = (MicroFxMeshRenderer){ 0 };
    static const char *vs =
        "#version 100\nprecision highp float;\n"
        "attribute vec3 aPosition;attribute vec3 aNormal;attribute float aObject;\n"
        "uniform mat4 uView;uniform mat4 uProjection;uniform vec4 uPositionScale[16];"
        "uniform vec4 uRotation[16];uniform vec4 uColor[16];uniform vec4 uEffect[16];"
        "varying lowp vec4 vColor;varying vec3 vLocal;varying vec3 vEffect;\n"
        "void fetch(int i,out vec4 p,out vec4 r,out vec4 c,out vec4 e){"
        "if(i==0){p=uPositionScale[0];r=uRotation[0];c=uColor[0];e=uEffect[0];}"
        "else if(i==1){p=uPositionScale[1];r=uRotation[1];c=uColor[1];e=uEffect[1];}"
        "else if(i==2){p=uPositionScale[2];r=uRotation[2];c=uColor[2];e=uEffect[2];}"
        "else if(i==3){p=uPositionScale[3];r=uRotation[3];c=uColor[3];e=uEffect[3];}"
        "else if(i==4){p=uPositionScale[4];r=uRotation[4];c=uColor[4];e=uEffect[4];}"
        "else if(i==5){p=uPositionScale[5];r=uRotation[5];c=uColor[5];e=uEffect[5];}"
        "else if(i==6){p=uPositionScale[6];r=uRotation[6];c=uColor[6];e=uEffect[6];}"
        "else if(i==7){p=uPositionScale[7];r=uRotation[7];c=uColor[7];e=uEffect[7];}"
        "else if(i==8){p=uPositionScale[8];r=uRotation[8];c=uColor[8];e=uEffect[8];}"
        "else if(i==9){p=uPositionScale[9];r=uRotation[9];c=uColor[9];e=uEffect[9];}"
        "else if(i==10){p=uPositionScale[10];r=uRotation[10];c=uColor[10];e=uEffect[10];}"
        "else if(i==11){p=uPositionScale[11];r=uRotation[11];c=uColor[11];e=uEffect[11];}"
        "else if(i==12){p=uPositionScale[12];r=uRotation[12];c=uColor[12];e=uEffect[12];}"
        "else if(i==13){p=uPositionScale[13];r=uRotation[13];c=uColor[13];e=uEffect[13];}"
        "else if(i==14){p=uPositionScale[14];r=uRotation[14];c=uColor[14];e=uEffect[14];}"
        "else{p=uPositionScale[15];r=uRotation[15];c=uColor[15];e=uEffect[15];}}\n"
        "vec3 rotate(vec3 p,vec3 r){vec3 c=cos(r),s=sin(r);"
        "p=vec3(p.x,c.x*p.y-s.x*p.z,s.x*p.y+c.x*p.z);"
        "p=vec3(c.y*p.x+s.y*p.z,p.y,-s.y*p.x+c.y*p.z);"
        "return vec3(c.z*p.x-s.z*p.y,s.z*p.x+c.z*p.y,p.z);}\n"
        "void main(){vec4 ps,rot,color,effect;fetch(int(aObject+0.5),ps,rot,color,effect);"
        "vec3 p=rotate(aPosition*ps.w,rot.xyz)+ps.xyz;vec3 n=normalize(rotate(aNormal,rot.xyz));"
        "vec3 light=normalize(vec3(0.45,0.78,0.35));float d=max(dot(n,light),0.0);"
        "float rim=pow(1.0-max(n.z,0.0),2.0);vec3 lit=color.rgb*(0.24+0.72*d)+rim*color.rgb*0.18;"
        "vColor=vec4(lit,color.a);vLocal=aPosition;vEffect=effect.xyz;"
        "gl_Position=uProjection*uView*vec4(p,1.0);}\n";
    static const char *fs =
        "#version 100\nprecision mediump float;varying lowp vec4 vColor;varying vec3 vLocal;"
        "varying vec3 vEffect;uniform float uTime;"
        MICROFX_GLSL_NOISE2
        "void main(){vec3 color=vColor.rgb;"
        "if(vEffect.x>0.5&&vEffect.x<1.5){float g=clamp(vLocal.y+0.5,0.0,1.0);color*=mix(1.0,mix(0.55,1.35,g),vEffect.y);}"
        "else if(vEffect.x>1.5&&vEffect.x<2.5){float n=microfxNoise2(vLocal.xy*max(vEffect.z,0.01)+uTime*0.2);color*=mix(1.0,0.45+n*1.1,vEffect.y);}"
        "else if(vEffect.x>2.5){float bands=0.5+0.5*sin((vLocal.y+uTime*0.35)*max(vEffect.z,1.0)*12.0);color*=mix(1.0,0.55+bands*0.75,vEffect.y);}"
        "gl_FragColor=vec4(color,vColor.a);}\n";
    GLuint vertex = Compile(GL_VERTEX_SHADER, vs);
    GLuint fragment = Compile(GL_FRAGMENT_SHADER, fs);
    if (!vertex || !fragment) return false;
    renderer->program = glCreateProgram();
    glAttachShader(renderer->program, vertex);
    glAttachShader(renderer->program, fragment);
    glBindAttribLocation(renderer->program, 0, "aPosition");
    glBindAttribLocation(renderer->program, 1, "aNormal");
    glBindAttribLocation(renderer->program, 2, "aObject");
    glLinkProgram(renderer->program);
    glDeleteShader(vertex); glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(renderer->program, GL_LINK_STATUS, &linked);
    if (!linked) return false;
    renderer->viewLocation = glGetUniformLocation(renderer->program, "uView");
    renderer->projectionLocation = glGetUniformLocation(renderer->program, "uProjection");
    renderer->positionScaleLocation = glGetUniformLocation(renderer->program, "uPositionScale");
    renderer->rotationLocation = glGetUniformLocation(renderer->program, "uRotation");
    renderer->colorLocation = glGetUniformLocation(renderer->program, "uColor");
    renderer->effectLocation = glGetUniformLocation(renderer->program, "uEffect");
    renderer->timeLocation = glGetUniformLocation(renderer->program, "uTime");
    glGenBuffers(1, &renderer->vertexBuffer);
    return renderer->vertexBuffer != 0;
}

static void Put(MeshVertex *out, int *cursor, float x, float y, float z,
                float nx, float ny, float nz, int object)
{
    MeshVertex *v = &out[(*cursor)++];
    v->position[0]=x; v->position[1]=y; v->position[2]=z;
    v->normal[0]=nx; v->normal[1]=ny; v->normal[2]=nz;
    v->object=(float)object;
}

static void Cube(MeshVertex *out, int *cursor, int object)
{
    static const float faces[6][12] = {
        { 1,0,0, .5f,-.5f,-.5f, .5f,.5f,-.5f, .5f,.5f,.5f },
        {-1,0,0,-.5f,-.5f,.5f,-.5f,.5f,.5f,-.5f,.5f,-.5f },
        {0, 1,0,-.5f,.5f,-.5f,-.5f,.5f,.5f,.5f,.5f,.5f },
        {0,-1,0,-.5f,-.5f,.5f,-.5f,-.5f,-.5f,.5f,-.5f,-.5f },
        {0,0, 1,-.5f,-.5f,.5f,.5f,-.5f,.5f,.5f,.5f,.5f },
        {0,0,-1,.5f,-.5f,-.5f,-.5f,-.5f,-.5f,-.5f,.5f,-.5f }
    };
    for (int f=0; f<6; f++) {
        const float *q=faces[f];
        const int order[6]={0,1,2,0,2,3};
        float p[4][3]={{q[3],q[4],q[5]},{q[6],q[7],q[8]},
                       {q[9],q[10],q[11]},{q[3]+q[9]-q[6],q[4]+q[10]-q[7],q[5]+q[11]-q[8]}};
        for (int i=0;i<6;i++) Put(out,cursor,p[order[i]][0],p[order[i]][1],p[order[i]][2],q[0],q[1],q[2],object);
    }
}

static void Box(MeshVertex *out,int *cursor,int object,float cx,float cy,float cz,
                float sx,float sy,float sz)
{
    MeshVertex cube[36];int count=0;
    Cube(cube,&count,object);
    for(int i=0;i<count;i++){
        Put(out,cursor,cx+cube[i].position[0]*sx,
            cy+cube[i].position[1]*sy,cz+cube[i].position[2]*sz,
            cube[i].normal[0],cube[i].normal[1],cube[i].normal[2],object);
    }
}

static void WireCube(MeshVertex *out,int *cursor,int object)
{
    const float edge=0.035f;
    for(int axis=0;axis<3;axis++)for(int a=-1;a<=1;a+=2)for(int b=-1;b<=1;b+=2){
        float center[3]={0},size[3]={edge,edge,edge};
        center[(axis+1)%3]=a*0.5f;center[(axis+2)%3]=b*0.5f;size[axis]=1.0f;
        Box(out,cursor,object,center[0],center[1],center[2],size[0],size[1],size[2]);
    }
}

static void GridQuad(MeshVertex *out,int *cursor,int object,float x0,float z0,
                     float x1,float z1)
{
    Put(out,cursor,x0,0,z0,0,1,0,object);Put(out,cursor,x0,0,z1,0,1,0,object);
    Put(out,cursor,x1,0,z1,0,1,0,object);Put(out,cursor,x0,0,z0,0,1,0,object);
    Put(out,cursor,x1,0,z1,0,1,0,object);Put(out,cursor,x1,0,z0,0,1,0,object);
}

static void Grid(MeshVertex *out,int *cursor,int object)
{
    // Wide enough to survive perspective minification on non-MSAA RGB565
    // targets without turning the near grid into heavy slabs.
    const float half=0.5f,width=0.0035f;
    for(int i=0;i<=8;i++){
        float p=-half+(float)i/8.0f;
        GridQuad(out,cursor,object,-half,p-width,half,p+width);
        GridQuad(out,cursor,object,p-width,-half,p+width,half);
    }
}

static void SpherePoint(MeshVertex *out, int *cursor, float latitude,
                        float longitude, int object)
{
    float c=cosf(latitude), x=c*cosf(longitude), y=sinf(latitude), z=c*sinf(longitude);
    Put(out,cursor,x*.5f,y*.5f,z*.5f,x,y,z,object);
}

static void Sphere(MeshVertex *out, int *cursor, int object)
{
    const float pi=3.14159265358979323846f;
    for (int ring=0;ring<SPHERE_RINGS;ring++) {
        float a0=-pi*.5f+pi*(float)ring/SPHERE_RINGS;
        float a1=-pi*.5f+pi*(float)(ring+1)/SPHERE_RINGS;
        for (int slice=0;slice<SPHERE_SLICES;slice++) {
            float b0=2*pi*(float)slice/SPHERE_SLICES;
            float b1=2*pi*(float)(slice+1)/SPHERE_SLICES;
            SpherePoint(out,cursor,a0,b0,object); SpherePoint(out,cursor,a1,b0,object);
            SpherePoint(out,cursor,a1,b1,object); SpherePoint(out,cursor,a0,b0,object);
            SpherePoint(out,cursor,a1,b1,object); SpherePoint(out,cursor,a0,b1,object);
        }
    }
}

static int TriangleVertexCount(const Mesh *mesh)
{
    return mesh->indices ? mesh->triangleCount*3 : mesh->vertexCount;
}

static int ModelVertexCount(const Model *model)
{
    int count=0;
    for(int i=0;i<model->meshCount;i++)count+=TriangleVertexCount(&model->meshes[i]);
    return count;
}

static void ModelGeometry(MeshVertex *out,int *cursor,const Model *model,int object)
{
    BoundingBox bounds=GetModelBoundingBox(*model);
    Vector3 center={(bounds.min.x+bounds.max.x)*0.5f,
                    (bounds.min.y+bounds.max.y)*0.5f,
                    (bounds.min.z+bounds.max.z)*0.5f};
    float width=bounds.max.x-bounds.min.x,height=bounds.max.y-bounds.min.y;
    float depth=bounds.max.z-bounds.min.z;
    float largest=fmaxf(width,fmaxf(height,depth));
    float normalize=largest>0.0f?1.0f/largest:1.0f;
    for(int m=0;m<model->meshCount;m++){
        const Mesh *mesh=&model->meshes[m];
        int count=TriangleVertexCount(mesh);
        for(int i=0;i<count;i++){
            int source=mesh->indices?mesh->indices[i]:i;
            const float *position=&mesh->vertices[source*3];
            float nx=0.0f,ny=1.0f,nz=0.0f;
            if(mesh->normals){
                nx=mesh->normals[source*3];ny=mesh->normals[source*3+1];
                nz=mesh->normals[source*3+2];
            }
            Put(out,cursor,(position[0]-center.x)*normalize,
                (position[1]-center.y)*normalize,
                (position[2]-center.z)*normalize,nx,ny,nz,object);
        }
    }
}

static bool Rebuild(MicroFxMeshRenderer *renderer, MicroFxScene *scene)
{
    Model models[MICROFX_MAX_MESH_ELEMENTS]={0};
    int capacity=0;
    for(int i=0;i<scene->meshCount;i++){
        if(!scene->mesh[i].visible)continue;
        if(scene->mesh[i].kind==MICROFX_MESH_CUBE)capacity+=36;
        else if(scene->mesh[i].kind==MICROFX_MESH_SPHERE)
            capacity+=SPHERE_RINGS*SPHERE_SLICES*6;
        else if(scene->mesh[i].kind==MICROFX_MESH_WIRE_CUBE)capacity+=12*36;
        else if(scene->mesh[i].kind==MICROFX_MESH_GRID)capacity+=18*6;
        else{
            models[i]=LoadModel(scene->mesh[i].assetPath);
            if(!IsModelValid(models[i])){
                fprintf(stderr,"MICROFX_MESH failed to load asset: %s\n",
                        scene->mesh[i].assetPath);
                for(int j=0;j<=i;j++)if(IsModelValid(models[j]))UnloadModel(models[j]);
                renderer->vertexCount=0;
                return false;
            }
            capacity+=ModelVertexCount(&models[i]);
        }
    }
    MeshVertex *vertices=calloc((size_t)capacity,sizeof(*vertices));
    if(!vertices&&capacity>0){
        for(int i=0;i<scene->meshCount;i++)if(IsModelValid(models[i]))UnloadModel(models[i]);
        renderer->vertexCount=0;
        return false;
    }
    int cursor=0;
    for (int i=0;i<scene->meshCount;i++) {
        if(!scene->mesh[i].visible)continue;
        if (scene->mesh[i].kind==MICROFX_MESH_CUBE) Cube(vertices,&cursor,i);
        else if(scene->mesh[i].kind==MICROFX_MESH_SPHERE)Sphere(vertices,&cursor,i);
        else if(scene->mesh[i].kind==MICROFX_MESH_WIRE_CUBE)WireCube(vertices,&cursor,i);
        else if(scene->mesh[i].kind==MICROFX_MESH_GRID)Grid(vertices,&cursor,i);
        else ModelGeometry(vertices,&cursor,&models[i],i);
    }
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(cursor*sizeof(*vertices)),vertices,GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    renderer->vertexCount=cursor;
    scene->meshGeometryDirty=false;
    free(vertices);
    for(int i=0;i<scene->meshCount;i++)if(IsModelValid(models[i]))UnloadModel(models[i]);
    return true;
}

static void DecodeColor(float *out, uint32_t color)
{
    out[0]=((color>>24)&255)/255.0f; out[1]=((color>>16)&255)/255.0f;
    out[2]=((color>>8)&255)/255.0f; out[3]=(color&255)/255.0f;
}

bool MicroFxMeshRendererDraw(MicroFxMeshRenderer *renderer, MicroFxScene *scene,
                            const float *view, const float *projection)
{
    if(!renderer->program)return false;
    if(scene->meshCount==0)return true;
    if(scene->meshGeometryDirty&&!Rebuild(renderer,scene))return false;
    float positionScale[MICROFX_MAX_MESH_ELEMENTS*4]={0};
    float rotation[MICROFX_MAX_MESH_ELEMENTS*4]={0};
    float color[MICROFX_MAX_MESH_ELEMENTS*4]={0};
    float effect[MICROFX_MAX_MESH_ELEMENTS*4]={0};
    for (int i=0;i<scene->meshCount;i++) {
        const MicroFxMeshElement *e=&scene->mesh[i];
        positionScale[i*4]=e->position[0]; positionScale[i*4+1]=e->position[1];
        positionScale[i*4+2]=e->position[2]; positionScale[i*4+3]=e->visible?e->scale:0.0f;
        rotation[i*4]=e->rotation[0]; rotation[i*4+1]=e->rotation[1]; rotation[i*4+2]=e->rotation[2];
        DecodeColor(&color[i*4],e->color);
        effect[i*4]=e->effect[0];effect[i*4+1]=e->effect[1];effect[i*4+2]=e->effect[2];
    }
    glUseProgram(renderer->program);
    glUniformMatrix4fv(renderer->viewLocation,1,GL_FALSE,view);
    glUniformMatrix4fv(renderer->projectionLocation,1,GL_FALSE,projection);
    glUniform4fv(renderer->positionScaleLocation,MICROFX_MAX_MESH_ELEMENTS,positionScale);
    glUniform4fv(renderer->rotationLocation,MICROFX_MAX_MESH_ELEMENTS,rotation);
    glUniform4fv(renderer->colorLocation,MICROFX_MAX_MESH_ELEMENTS,color);
    glUniform4fv(renderer->effectLocation,MICROFX_MAX_MESH_ELEMENTS,effect);
    glUniform1f(renderer->timeLocation,scene->time);
    glBindBuffer(GL_ARRAY_BUFFER,renderer->vertexBuffer);
    for (int i=0;i<3;i++) glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(MeshVertex),(void *)offsetof(MeshVertex,position));
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(MeshVertex),(void *)offsetof(MeshVertex,normal));
    glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,sizeof(MeshVertex),(void *)offsetof(MeshVertex,object));
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    glDrawArrays(GL_TRIANGLES,0,renderer->vertexCount);
    glDisable(GL_CULL_FACE);
    for (int i=0;i<3;i++) glDisableVertexAttribArray((GLuint)i);
    glBindBuffer(GL_ARRAY_BUFFER,0); glUseProgram(0);
    scene->meshStateDirty=false;
    return true;
}

void MicroFxMeshRendererDestroy(MicroFxMeshRenderer *renderer)
{
    if (renderer->vertexBuffer) glDeleteBuffers(1,&renderer->vertexBuffer);
    if (renderer->program) glDeleteProgram(renderer->program);
    *renderer=(MicroFxMeshRenderer){0};
}
