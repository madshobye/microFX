#ifndef MICROFX_NETWORK_H
#define MICROFX_NETWORK_H

#include <stdbool.h>
#include <quickjs/quickjs.h>

typedef struct MicroFxNetwork MicroFxNetwork;

MicroFxNetwork *MicroFxNetworkCreate(JSContext *context, JSValueConst fx);
bool MicroFxNetworkPump(MicroFxNetwork *network);
void MicroFxNetworkDestroy(MicroFxNetwork *network);

#endif
