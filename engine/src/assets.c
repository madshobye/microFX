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
    char candidate[PATH_MAX];
    int length=snprintf(candidate,sizeof(candidate),"%s/%s",projectRoot,assetPath);
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
