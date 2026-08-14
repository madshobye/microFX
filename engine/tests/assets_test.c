#define _XOPEN_SOURCE 700
#include "microfx/assets.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 7);
    char root[1024], resolved[1024], error[128];
    assert(MicroFxProjectRoot(argv[1],root,sizeof(root),error,sizeof(error)));
    assert(MicroFxResolveAsset(root,"model.obj",resolved,sizeof(resolved),
                              error,sizeof(error)));
    char expected[PATH_MAX];
    assert(realpath(argv[2],expected));
    assert(strcmp(resolved,expected) == 0);
    assert(MicroFxResolveAsset(root,"assets/model.obj",resolved,sizeof(resolved),
                              error,sizeof(error)));
    assert(strcmp(resolved,expected) == 0);
    assert(!MicroFxResolveAsset(root,"../outside.obj",resolved,sizeof(resolved),
                               error,sizeof(error)));
    assert(!MicroFxResolveAsset(root,"escape.obj",resolved,sizeof(resolved),
                               error,sizeof(error)));
    assert(!MicroFxResolveAsset(root,argv[3],resolved,sizeof(resolved),
                               error,sizeof(error)));
    assert(!MicroFxResolveAsset(root,"missing.obj",resolved,sizeof(resolved),
                               error,sizeof(error)));
    assert(setenv("MICROFX_DATA_ROOT",argv[4],1)==0);
    assert(MicroFxResolveDataAsset(root,"feed.json",resolved,sizeof(resolved),
                                  error,sizeof(error)));
    assert(realpath(argv[5],expected));
    assert(strcmp(resolved,expected)==0);
    assert(!MicroFxResolveDataAsset(root,"../outside.obj",resolved,sizeof(resolved),
                                   error,sizeof(error)));
    assert(unsetenv("MICROFX_DATA_ROOT")==0);
    puts(argv[6]);
    return 0;
}
