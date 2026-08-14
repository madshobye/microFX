#define _XOPEN_SOURCE 700
#include "microfx/assets.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool CopyResult(const char *value, char *output, size_t outputSize,
                       char *error, size_t errorSize)
{
    int length=snprintf(output,outputSize,"%s",value);
    if(length<0||(size_t)length>=outputSize){
        snprintf(error,errorSize,"resolved path is too long");
        return false;
    }
    return true;
}

bool MicroFxProjectRoot(const char *scriptPath, char *output, size_t outputSize,
                       char *error, size_t errorSize)
{
    char resolved[PATH_MAX];
    if(!scriptPath||!realpath(scriptPath,resolved)){
        snprintf(error,errorSize,"cannot resolve project script");
        return false;
    }
    char *slash=strrchr(resolved,'/');
    if(!slash){snprintf(error,errorSize,"project script has no directory");return false;}
    if(slash==resolved)slash[1]='\0';else *slash='\0';
    return CopyResult(resolved,output,outputSize,error,errorSize);
}

bool MicroFxResolveAsset(const char *projectRoot, const char *assetPath,
                        char *output, size_t outputSize,
                        char *error, size_t errorSize)
{
    if(!projectRoot||!assetPath||!assetPath[0]||assetPath[0]=='/'){
        snprintf(error,errorSize,"asset path must be project-relative");
        return false;
    }
    /* Public project assets live below assets/.  Keep an explicit assets/ path
       valid as well so generated projects and hand-written projects agree. */
    const char *separator=strncmp(assetPath,"assets/",7)==0?"/":"/assets/";
    char candidate[PATH_MAX];
    int length=snprintf(candidate,sizeof(candidate),"%s%s%s",projectRoot,separator,assetPath);
    if(length<0||(size_t)length>=sizeof(candidate)){
        snprintf(error,errorSize,"asset path is too long");
        return false;
    }
    char resolved[PATH_MAX];
    if(!realpath(candidate,resolved)){
        snprintf(error,errorSize,"asset does not exist");
        return false;
    }
    size_t rootLength=strlen(projectRoot);
    bool rootIsSlash=rootLength==1&&projectRoot[0]=='/';
    if(strncmp(resolved,projectRoot,rootLength)!=0||
       (!rootIsSlash&&resolved[rootLength]!='/'&&resolved[rootLength]!='\0')){
        snprintf(error,errorSize,"asset escapes the project directory");
        return false;
    }
    struct stat info;
    if(stat(resolved,&info)!=0||!S_ISREG(info.st_mode)||access(resolved,R_OK)!=0){
        snprintf(error,errorSize,"asset is not a readable regular file");
        return false;
    }
    return CopyResult(resolved,output,outputSize,error,errorSize);
}

bool MicroFxResolveDataAsset(const char *projectRoot, const char *assetPath,
                            char *output, size_t outputSize,
                            char *error, size_t errorSize)
{
    if(!projectRoot||!assetPath||!assetPath[0]||assetPath[0]=='/'){
        snprintf(error,errorSize,"data path must be project-relative");
        return false;
    }
    const char *project=strrchr(projectRoot,'/');
    project=project?project+1:projectRoot;
    if(!project[0]||strchr(project,'/')||strcmp(project,".")==0||strcmp(project,"..")==0){
        snprintf(error,errorSize,"invalid project directory");
        return false;
    }
    const char *volatileRoot=getenv("MICROFX_DATA_ROOT");
    if(!volatileRoot||!volatileRoot[0])volatileRoot="/run/microfx-data";
    const char *relative=strncmp(assetPath,"assets/",7)==0?assetPath+7:assetPath;
    char overlay[PATH_MAX],overlayProject[PATH_MAX];
    int rootLength=snprintf(overlayProject,sizeof(overlayProject),"%s/%s",volatileRoot,project);
    int pathLength=snprintf(overlay,sizeof(overlay),"%s/%s",overlayProject,relative);
    if(rootLength>0&&(size_t)rootLength<sizeof(overlayProject)&&
       pathLength>0&&(size_t)pathLength<sizeof(overlay)){
        char resolvedRoot[PATH_MAX],resolved[PATH_MAX];
        if(realpath(overlayProject,resolvedRoot)&&realpath(overlay,resolved)){
            size_t length=strlen(resolvedRoot);
            struct stat info;
            if(strncmp(resolved,resolvedRoot,length)==0&&resolved[length]=='/'&&
               stat(resolved,&info)==0&&S_ISREG(info.st_mode)&&access(resolved,R_OK)==0)
                return CopyResult(resolved,output,outputSize,error,errorSize);
        }
    }
    return MicroFxResolveAsset(projectRoot,assetPath,output,outputSize,error,errorSize);
}
