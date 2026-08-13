#define _XOPEN_SOURCE 700
#include "microfx/assets.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 5);
    char root[1024], resolved[1024], error[128];
    assert(MicroFxProjectRoot(argv[1],root,sizeof(root),error,sizeof(error)));
    assert(MicroFxResolveAsset(root,"model.obj",resolved,sizeof(resolved),
                              error,sizeof(error)));
    char expected[PATH_MAX];
    assert(realpath(argv[2],expected));
    assert(strcmp(resolved,expected) == 0);
    assert(!MicroFxResolveAsset(root,"../outside.obj",resolved,sizeof(resolved),
                               error,sizeof(error)));
    assert(!MicroFxResolveAsset(root,"escape.obj",resolved,sizeof(resolved),
                               error,sizeof(error)));
    assert(!MicroFxResolveAsset(root,argv[3],resolved,sizeof(resolved),
                               error,sizeof(error)));
    assert(!MicroFxResolveAsset(root,"missing.obj",resolved,sizeof(resolved),
                               error,sizeof(error)));
    puts(argv[4]);
    return 0;
}
