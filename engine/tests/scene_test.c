#include "microfx/scene.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    MicroFxScene scene;
    MicroFxSceneInit(&scene);
    assert(scene.camera.fovY == 48.0f);
    assert(scene.runtime.targetFps == 30 && scene.runtime.debugBarUntilSeconds == 600.0f);
    assert(scene.runtime.debugBarStyle == MICROFX_DEBUG_BAR_STANDARD);
    assert(scene.runtime.durationSeconds == 0.0f);
    assert(!scene.runtime.profiling && scene.runtime.profileIntervalFrames == 120);
    assert(scene.runtime.densitySampleFrames == 60);
    assert(scene.runtime.densityStep == 0.1f);
    assert(scene.runtime.densityDownThreshold == 1.08f);
    assert(scene.runtime.densityUpThreshold == 0.72f);
    assert(scene.runtime.densityUpSamples == 4);

    int circle=MicroFxSceneAddCircle(&scene,10,20,5,0xff00ffff);
    assert((circle&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_SDF);
    assert(MicroFxSceneMove(&scene,circle,30,40,0.5f));
    assert(scene.sdf[0].x==30 && scene.sdf[0].rotation==0.5f);
    assert(MicroFxSceneSetSdfGeometry(&scene,circle,
                                      MICROFX_SDF_ROUNDED_RECT,20,12,4));
    assert(scene.sdf[0].kind==MICROFX_SDF_ROUNDED_RECT &&
           scene.sdf[0].width==20 && scene.sdf[0].height==12 &&
           scene.sdf[0].radius==4);

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
    assert(scene.quad[0].opacity==1.0f);
    assert(MicroFxSceneSetOpacity(&scene,quad,0.4f));
    assert(scene.quad[0].opacity==0.4f && scene.quadDirty);
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
    assert(MicroFxSceneSetMeshShader(&scene,model,"/project/shaders/custom.vs",
                                     "/project/shaders/custom.fs"));
    assert(scene.mesh[1].shaderIndex==0 && scene.meshShaderCount==1);
    assert(strcmp(scene.meshShader[0].fragmentPath,
                  "/project/shaders/custom.fs")==0);
    assert(MicroFxSceneSetMeshShader(&scene,cube,"/project/shaders/custom.vs",
                                     "/project/shaders/custom.fs"));
    assert(scene.mesh[0].shaderIndex==0 && scene.meshShaderCount==1);
    int wire=MicroFxSceneAddWireCube(&scene,0,1,2,3,0xffffffff);
    int grid=MicroFxSceneAddGrid(&scene,0,0,0,10,0x7aa8d0ff);
    assert(wire>=0 && grid>=0);
    assert(scene.mesh[2].kind==MICROFX_MESH_WIRE_CUBE);
    assert(scene.mesh[3].kind==MICROFX_MESH_GRID);
    for(int index=scene.meshCount;index<20;index++)
        assert(MicroFxSceneAddCube(&scene,index,0,0,1,0xffffffff)>=0);
    assert(scene.meshCount==20);

    int text=MicroFxSceneAddText(&scene,"hello",10,20,24,0xffffffff);
    assert((text&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_TEXT);
    assert(MicroFxSceneSetText(&scene,text,"updated"));
    assert(strcmp(scene.text[0].text,"updated")==0 && scene.textDirty);
    scene.textDirty=false;
    assert(MicroFxSceneMove(&scene,text,30,40,0.25f));
    assert(scene.text[0].x==30 && scene.text[0].y==40 &&
           scene.text[0].rotation==0.25f && scene.textDirty);
    scene.textDirty=false;
    assert(MicroFxSceneSetTextFont(&scene,text,"/project/assets/display.ttf"));
    assert(strcmp(scene.text[0].fontPath,"/project/assets/display.ttf")==0);
    assert(scene.textDirty);
    scene.textDirty=false;
    assert(MicroFxSceneSetTextFont(&scene,text,"/project/assets/display.ttf"));
    assert(!scene.textDirty);
    assert(MicroFxSceneSetTextFont(&scene,text,""));
    assert(scene.text[0].fontPath[0]=='\0');
    assert(!MicroFxSceneSetTextFont(&scene,cube,"font.ttf"));
    assert(MicroFxSceneSetColor(&scene,text,0x12345678));
    assert(scene.text[0].opacity==1.0f);
    assert(MicroFxSceneSetOpacity(&scene,text,0.65f));
    assert(scene.text[0].opacity==0.65f && scene.textDirty);
    assert(MicroFxSceneSetVisible(&scene, quad, false));
    assert(!scene.quad[0].visible && scene.quadDirty);
    assert(MicroFxSceneSetVisible(&scene, quad, true));
    assert(scene.quad[0].visible);
    assert(scene.text[0].color==0x12345678);

    int image=MicroFxSceneAddImage(&scene,"/project/assets/icon.png",
                                   100,200,1.5f,0xffffffff);
    assert((image&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_IMAGE);
    assert(strcmp(scene.image[0].assetPath,"/project/assets/icon.png")==0);
    scene.imageDirty=false;
    assert(MicroFxSceneMove(&scene,image,300,400,0.75f));
    assert(scene.image[0].x==300 && scene.image[0].rotation==0.75f);
    assert(!scene.imageDirty);
    assert(MicroFxSceneSetColor(&scene,image,0x89abcdef));
    assert(scene.image[0].tint==0x89abcdef);

    int tileMap=MicroFxSceneAddTileMap(&scene,1.0f,1.2f,0.4f,1.0f,0x18283cff);
    assert(tileMap==0 && scene.tileMapCount==1);
    assert(MicroFxSceneBeginTileMap(&scene,tileMap,1,1));
    const uint8_t png[]={1,2,3,4};
    assert(MicroFxSceneSubmitTileMapTile(&scene,tileMap,1,0,-10,20,256,
                                         png,sizeof(png)));
    assert(scene.tileMap[0].tiles[0].received &&
           scene.tileMap[0].tiles[0].encodedSize==sizeof(png));
    assert(MicroFxSceneSetTileMapVisible(&scene,tileMap,false));
    assert(!scene.tileMap[0].visible);
    int gpuTexture=MicroFxSceneAddGpuMapTexture(&scene,tileMap);
    assert((gpuTexture&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_GPU_TEXTURE);
    assert(MicroFxSceneSetGpuTextureShader(&scene,gpuTexture,
                                           "/project/assets/shaders/map.fs"));
    assert(scene.gpuTexture[0].shaderVersion==1);
    assert(MicroFxSceneSetGpuTextureSecondaryMap(&scene,gpuTexture,tileMap));
    assert(scene.gpuTexture[0].secondary&&
           scene.gpuTexture[0].secondaryMapIndex==tileMap&&
           scene.gpuTexture[0].shaderVersion==2);
    assert(MicroFxSceneSetGpuTextureSecondaryAsset(
        &scene,gpuTexture,"/project/assets/night.png"));
    assert(scene.gpuTexture[0].secondarySource==MICROFX_GPU_TEXTURE_ASSET&&
           strcmp(scene.gpuTexture[0].secondaryAssetPath,
                  "/project/assets/night.png")==0);
    const float mapParams[]={1,2,3,4,5};
    assert(MicroFxSceneSetGpuTextureParams(&scene,gpuTexture,mapParams,5));
    assert(scene.gpuTexture[0].paramCount==5&&scene.gpuTexture[0].params[4]==5);
    const uint8_t field[]={1,2,3,4,5,6,7,8};
    assert(MicroFxSceneSetGpuTextureField(&scene,gpuTexture,2,1,field,sizeof(field)));
    assert(scene.gpuTexture[0].fieldWidth==2&&
           scene.gpuTexture[0].fieldHeight==1&&
           scene.gpuTexture[0].fieldRgba[7]==8);
    assert(!MicroFxSceneSetGpuTextureField(&scene,gpuTexture,65,1,
                                            field,sizeof(field)));
    assert(MicroFxSceneSetGpuTextureStage(&scene,gpuTexture,
                                           MICROFX_GPU_TEXTURE_BACKGROUND));
    assert(MicroFxSceneSetGpuTextureBlend(&scene,gpuTexture,false));
    assert(!scene.gpuTexture[0].blend);
    assert(MicroFxSceneSetGpuTextureOpacity(&scene,gpuTexture,0.35f));
    assert(scene.gpuTexture[0].opacity==0.35f);
    assert(!MicroFxSceneSetGpuTextureOpacity(&scene,gpuTexture,1.1f));
    assert(MicroFxSceneBeginTileMap(&scene,tileMap,2,1));
    assert(!scene.imageDirty);
    assert(MicroFxSceneSetOpacity(&scene,image,0.5f));
    assert(scene.image[0].opacity==0.5f);
    assert(!scene.imageDirty);
    assert(MicroFxSceneSetImageScale(&scene,image,2.0f));
    assert(scene.image[0].scale==2.0f);
    assert(scene.imageDirty);
    scene.imageDirty=false;
    assert(MicroFxSceneSetVisible(&scene,image,false));
    assert(!scene.image[0].visible && !scene.imageDirty);
    assert(MicroFxSceneAddImage(&scene,"",0,0,1,0xffffffff)<0);
    int backgroundImage=MicroFxSceneAddBackgroundImage(
        &scene,"/project/assets/map.png",0xffffffff);
    assert(backgroundImage>=0 && scene.image[1].background);
    assert(MicroFxSceneAddImage(&scene,"bad.png",0,0,0,0xffffffff)<0);

    const float outlinePoints[][2]={{-1,1},{0,-1},{1,1}};
    int outline=MicroFxSceneAddOutline(&scene,outlinePoints,3,20,30,12,2,
                                       0xffffffff,true);
    assert((outline&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_OUTLINE);
    assert(MicroFxSceneMove(&scene,outline,40,50,0.25f));
    assert(scene.outline[0].x==40 && scene.outlineDirty);
    assert(MicroFxSceneSetOutlineScale(&scene,outline,15));
    assert(scene.outline[0].scale==15);
    assert(MicroFxSceneSetOutlinePoints(&scene,outline,outlinePoints,3));
    assert(MicroFxSceneSetColor(&scene,outline,0xabcdef12));
    assert(MicroFxSceneSetOpacity(&scene,outline,0.5f));
    assert(MicroFxSceneSetVisible(&scene,outline,false));
    assert(!scene.outline[0].visible);
    int polygon=MicroFxSceneAddPolygon(&scene,outlinePoints,3,20,30,12,
                                       0x10203080);
    assert((polygon&MICROFX_HANDLE_KIND_MASK)==MICROFX_HANDLE_OUTLINE);
    assert(scene.outline[1].filled&&scene.outline[1].closed);
    assert(MicroFxSceneSetOpacity(&scene,polygon,0.25f));

    assert(!MicroFxSceneMove(&scene,cube,0,0,0));
    assert(!MicroFxSceneTransform(&scene,circle,0,0,0,0,0,0,1));
    assert(!MicroFxSceneSetEffect(&scene,cube,99,1,1));
    assert(!MicroFxSceneSetOpacity(&scene,cube,0.5f));
    assert(!MicroFxSceneSetOpacity(&scene,quad,-0.1f));
    assert(!MicroFxSceneSetOpacity(&scene,quad,1.1f));
    return 0;
}
