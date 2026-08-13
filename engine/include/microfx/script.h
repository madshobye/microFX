#ifndef MICROFX_SCRIPT_H
#define MICROFX_SCRIPT_H

#include <stdbool.h>
#include "microfx/scene.h"

typedef struct MicroFxScript MicroFxScript;

MicroFxScript *MicroFxScriptCreate(MicroFxScene *scene, const char *path);
bool MicroFxScriptUpdate(MicroFxScript *script, double time, double delta);
void MicroFxScriptDestroy(MicroFxScript *script);

#endif
