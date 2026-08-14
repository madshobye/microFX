#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MICROFX_APPS_DIR "/tmp/microfx-control-test/apps"
#define MICROFX_RELOAD_SIGNAL "/tmp/microfx-control-test/reload"
#define MICROFX_RELOAD_STATUS "/tmp/microfx-control-test/status"
#define MICROFX_NETWORK_STATUS "/tmp/microfx-control-test/network"
#define MICROFX_PROVISION_STATUS "/tmp/microfx-control-test/provision"
#define MICROFX_PEER_ID_FILE "/tmp/microfx-control-test/peer-id"
#define main microfx_control_cgi_main
#include "../src/control.c"
#undef main

int main(void)
{
    system("rm -rf /tmp/microfx-control-test");
    assert(mkdir("/tmp/microfx-control-test",0700)==0);
    assert(mkdir(MICROFX_APPS_DIR,0700)==0);
    assert(mkdir(MICROFX_APPS_DIR "/projects",0700)==0);
    assert(mkdir(MICROFX_APPS_DIR "/projects/demo",0700)==0);
    assert(mkdir(MICROFX_APPS_DIR "/projects/clock",0700)==0);
    FILE *metadata=fopen(MICROFX_APPS_DIR "/projects/clock/project.json","w"); assert(metadata);
    fputs("{\"title\": \"Twenty Four Hour Clock\"}\n",metadata); fclose(metadata);
    assert(ValidName("clock"));
    assert(!ValidName("../escape"));
    assert(ProjectExists("demo"));
    assert(symlink("/tmp",MICROFX_APPS_DIR "/projects/escaped")==0);
    assert(!ProjectExists("escaped"));
    char title[97]; ProjectTitle("clock",title,sizeof(title));
    assert(strcmp(title,"Twenty Four Hour Clock")==0);
    ProjectTitle("demo",title,sizeof(title)); assert(strcmp(title,"demo")==0);
    char token[129];
    assert(Activate("demo",token,sizeof(token))==0);
    assert(strncmp(token,"portal-",7)==0);
    char active[65]; Current(active,sizeof(active));
    assert(strcmp(active,"demo")==0);
    assert(access(MICROFX_RELOAD_SIGNAL,F_OK)==0);
    FILE *reload=fopen(MICROFX_RELOAD_SIGNAL,"r"); assert(reload);
    char request[256]; assert(fgets(request,sizeof(request),reload)); fclose(reload);
    assert(strstr(request,"\tdemo\n")!=NULL);
    unlink(MICROFX_RELOAD_SIGNAL);
    assert(Activate("clock",token,sizeof(token))==0);
    Current(active,sizeof(active));
    assert(strcmp(active,"clock")==0);
    char priorToken[129]; snprintf(priorToken,sizeof(priorToken),"%s",token);
    unlink(MICROFX_RELOAD_SIGNAL);
    assert(mkdir(MICROFX_RELOAD_SIGNAL ".new",0700)==0);
    assert(Activate("demo",token,sizeof(token))!=0);
    Current(active,sizeof(active));
    assert(strcmp(active,"clock")==0);
    assert(rmdir(MICROFX_RELOAD_SIGNAL ".new")==0);
    assert(Activate("demo",token,sizeof(token))==0);
    assert(strcmp(token,priorToken)!=0);
    Current(active,sizeof(active));
    assert(strcmp(active,"demo")==0);
    assert(Activate("clock",token,sizeof(token))==0);
    assert(Activate("missing",token,sizeof(token))!=0);
    FILE *status=fopen(MICROFX_RELOAD_STATUS,"w"); assert(status);
    fputs("portal-test\tclock\trunning\trenderer passed health check\n",status); fclose(status);
    char activation[129],project[65],state[32],detail[256];
    Activation(activation,sizeof(activation),project,sizeof(project),state,sizeof(state),detail,sizeof(detail));
    assert(strcmp(activation,"portal-test")==0);
    assert(strcmp(project,"clock")==0);
    assert(strcmp(state,"running")==0);
    assert(strcmp(detail,"renderer passed health check")==0);
    FILE *network=fopen(MICROFX_NETWORK_STATUS,"w"); assert(network);
    fputs("state\tconnected\nssid\tTest Network\naddress\t192.0.2.4/24\nsignal\t-63\nbitrate\t72.2\ntxpower\t15.00\n",network); fclose(network);
    char value[128]; NetworkField("ssid",value,sizeof(value)); assert(strcmp(value,"Test Network")==0);
    NetworkField("signal",value,sizeof(value)); assert(strcmp(value,"-63")==0);
    FILE *provision=fopen(MICROFX_PROVISION_STATUS,"w"); assert(provision);
    fputs("state\thealthy\nradio\t1\nap_mode\t1\nlink\t1\naddress\t1\nportal\t1\nbeacon\t1\nfailures\t0\n",provision); fclose(provision);
    ProvisionField("state",value,sizeof(value)); assert(strcmp(value,"healthy")==0);
    ProvisionField("portal",value,sizeof(value)); assert(strcmp(value,"1")==0);
    ProvisionField("beacon",value,sizeof(value)); assert(strcmp(value,"1")==0);
    char peerId[65]; PeerId(peerId,sizeof(peerId)); assert(strcmp(peerId,"microfx-demo")==0);
    FILE *peer=fopen(MICROFX_PEER_ID_FILE,"w"); assert(peer);
    fputs("living-room-display\n",peer); fclose(peer);
    PeerId(peerId,sizeof(peerId)); assert(strcmp(peerId,"living-room-display")==0);
    peer=fopen(MICROFX_PEER_ID_FILE,"w"); assert(peer);
    fputs("../invalid\n",peer); fclose(peer);
    PeerId(peerId,sizeof(peerId)); assert(strcmp(peerId,"microfx-demo")==0);
    system("rm -rf /tmp/microfx-control-test");
    puts("provision control tests passed");
    return 0;
}
