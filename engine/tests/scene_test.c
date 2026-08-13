#include "microfx/scene.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    MicroFxScene scene;
    MicroFxSceneInit(&scene);
    assert(scene.camera.fovY == 48.0f);
    assert(scene.runtime.targetFps == 30 && scene.runtime.debugBar);
    assert(scene.runtime.durationSeconds == 0.0f);

    int circle=MicroFxSceneAddCircle(&scene,10,20,5,0xff00ffff);
    assert((circle&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_SDF);
    assert(MicroFxSceneMove(&scene,circle,30,40,0.5f));
    assert(scene.sdf[0].x==30 && scene.sdf[0].rotation==0.5f);

    int plainRect=MicroFxSceneAddRoundedRect(&scene,20,30,40,50,0,0xffffffff);
    int roundedRect=MicroFxSceneAddRoundedRect(&scene,20,30,40,50,6,0xffffffff);
    assert(plainRect>=0 && roundedRect>=0);
    assert(scene.sdf[1].kind==MICROFX_SDF_RECT);
    assert(scene.sdf[2].kind==MICROFX_SDF_ROUNDED_RECT);

    int quad=MicroFxSceneAddRect(&scene,100,200,300,40,0x12345678);
    int gradient=MicroFxSceneAddGradientRect(&scene,100,200,300,40,
                                             0x12345678,0x90abcdef);
    assert((quad&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_QUAD);
    assert((gradient&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_QUAD);
    assert(scene.quad[1].bottomColor==0x90abcdef);
    assert(MicroFxSceneMove(&scene,quad,20,30,0.25f));
    assert(scene.quad[0].x==20 && scene.quadDirty);
    int fastCircle=MicroFxSceneAddFastCircle(&scene,40,50,20,0xff00ffff);
    assert((fastCircle&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_QUAD);
    assert(scene.quad[2].kind==MICROFX_QUAD_CIRCLE);

    int cube=MicroFxSceneAddCube(&scene,0,1,2,3,0xffffffff);
    assert((cube&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_MESH);
    assert(MicroFxSceneTransform(&scene,cube,1,2,3,4,5,6,7));
    assert(scene.mesh[0].position[2]==3 && scene.mesh[0].scale==7);
    assert(MicroFxSceneSetEffect(&scene,cube,2,0.5f,4.0f));
    assert(scene.mesh[0].effect[0]==2 && scene.mesh[0].effect[2]==4.0f);
    int model=MicroFxSceneAddModel(&scene,"/project/models/demo.obj",
                                  0,1,2,3,0x65d9ffff);
    assert((model&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_MESH);
    assert(scene.mesh[1].kind==MICROFX_MESH_MODEL);
    assert(strcmp(scene.mesh[1].assetPath,"/project/models/demo.obj")==0);
    int wire=MicroFxSceneAddWireCube(&scene,0,1,2,3,0xffffffff);
    int grid=MicroFxSceneAddGrid(&scene,0,0,0,10,0x7aa8d0ff);
    assert(wire>=0 && grid>=0);
    assert(scene.mesh[2].kind==MICROFX_MESH_WIRE_CUBE);
    assert(scene.mesh[3].kind==MICROFX_MESH_GRID);

    int text=MicroFxSceneAddText(&scene,"hello",10,20,24,0xffffffff);
    assert((text&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_TEXT);
    assert(MicroFxSceneSetText(&scene,text,"updated"));
    assert(strcmp(scene.text[0].text,"updated")==0 && scene.textDirty);
    assert(MicroFxSceneSetColor(&scene,text,0x12345678));
    assert(scene.text[0].color==0x12345678);

    assert(!MicroFxSceneMove(&scene,cube,0,0,0));
    assert(!MicroFxSceneTransform(&scene,circle,0,0,0,0,0,0,1));
    assert(!MicroFxSceneSetEffect(&scene,cube,99,1,1));
    return 0;
}
