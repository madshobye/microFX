#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef MICROFX_APPS_DIR
#define MICROFX_APPS_DIR "/data/apps"
#endif
#ifndef MICROFX_RELOAD_SIGNAL
#define MICROFX_RELOAD_SIGNAL "/run/microfx-project-reload"
#endif
#ifndef MICROFX_RELOAD_STATUS
#define MICROFX_RELOAD_STATUS "/run/microfx-project-status"
#endif
#ifndef MICROFX_NETWORK_STATUS
#define MICROFX_NETWORK_STATUS "/run/microfx-network-status"
#endif
#ifndef MICROFX_PROVISION_STATUS
#define MICROFX_PROVISION_STATUS "/run/microfx-provision-status"
#endif
#ifndef MICROFX_PEER_ID_FILE
#define MICROFX_PEER_ID_FILE "/data/config/peer-id"
#endif

static int ValidName(const char *value)
{
    size_t length=strlen(value);
    if (length<1 || length>64 || value[0]=='.' || value[length-1]=='.') return 0;
    for (size_t i=0;i<length;i++) {
        unsigned char c=(unsigned char)value[i];
        if (!(isalnum(c) || c=='-' || c=='_' || c=='.')) return 0;
    }
    return 1;
}

static int ProjectExists(const char *name)
{
    char path[512]; struct stat information;
    if (!ValidName(name) || snprintf(path,sizeof(path),"%s/projects/%s",MICROFX_APPS_DIR,name)>=(int)sizeof(path)) return 0;
    return lstat(path,&information)==0 && S_ISDIR(information.st_mode);
}

static int JsonFileString(const char *path,const char *key,char *value,size_t capacity)
{
    value[0]='\0';
    FILE *file=fopen(path,"r");
    if (!file) return -1;
    char document[4097]; size_t length=fread(document,1,sizeof(document)-1,file); fclose(file);
    if (length==sizeof(document)-1) return -1;
    document[length]='\0';
    char needle[96];
    if (snprintf(needle,sizeof(needle),"\"%s\"",key)>=(int)sizeof(needle)) return -1;
    char *cursor=strstr(document,needle);
    if (!cursor) return -1;
    cursor+=strlen(needle);
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor++!=':') return -1;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor++!='\"') return -1;
    size_t output=0;
    while (*cursor && *cursor!='\"') {
        unsigned char c=(unsigned char)*cursor++;
        if (c=='\\') {
            c=(unsigned char)*cursor++;
            if (c=='n' || c=='r' || c=='t') c=' ';
            else if (c!='\"' && c!='\\' && c!='/') return -1;
        }
        if (c<32 || output+1>=capacity) return -1;
        value[output++]=(char)c;
    }
    if (*cursor!='\"' || output==0) { value[0]='\0'; return -1; }
    value[output]='\0';
    return 0;
}

static void ProjectTitle(const char *name,char *title,size_t capacity)
{
    char path[512];
    if (!ValidName(name) || snprintf(path,sizeof(path),"%s/projects/%s/project.json",MICROFX_APPS_DIR,name)>=(int)sizeof(path) ||
        JsonFileString(path,"title",title,capacity)) snprintf(title,capacity,"%s",name);
}

static int SignalReload(const char *project,char *token,size_t capacity)
{
    static unsigned long sequence;
    char temporary[512];
    sequence++;
    if (!ValidName(project) || snprintf(token,capacity,"portal-%ld-%ld-%lu",(long)getpid(),(long)time(NULL),sequence)>=(int)capacity ||
        snprintf(temporary,sizeof(temporary),"%s.new",MICROFX_RELOAD_SIGNAL)>=(int)sizeof(temporary)) return -1;
    FILE *file=fopen(temporary,"w");
    if (!file) return -1;
    if (fprintf(file,"%s\t%s\n",token,project)<0 || fclose(file) || chmod(temporary,0600) ||
        rename(temporary,MICROFX_RELOAD_SIGNAL)) { unlink(temporary); return -1; }
    return 0;
}

static int Activate(const char *name,char *token,size_t capacity)
{
    char target[512],temporary[512],current[512],previous[512];
    int hadPrevious=0;
    if (!ProjectExists(name)) return -1;
    if (snprintf(target,sizeof(target),"%s/projects/%s",MICROFX_APPS_DIR,name)>=(int)sizeof(target) ||
        snprintf(temporary,sizeof(temporary),"%s/current.new",MICROFX_APPS_DIR)>=(int)sizeof(temporary) ||
        snprintf(current,sizeof(current),"%s/current",MICROFX_APPS_DIR)>=(int)sizeof(current)) return -1;
    ssize_t previousLength=readlink(current,previous,sizeof(previous)-1);
    if (previousLength>=0) { previous[previousLength]='\0'; hadPrevious=1; }
    unlink(temporary);
    if (symlink(target,temporary) || rename(temporary,current)) { unlink(temporary); return -1; }
    if (SignalReload(name,token,capacity)==0) return 0;

    /* Selection and reload publication are one transaction from the portal's
       point of view. Restore the old selection if the request cannot be
       published, so a later restart cannot unexpectedly run the new project. */
    unlink(temporary);
    if (hadPrevious) {
        if (symlink(previous,temporary)==0 && rename(temporary,current)==0) return -1;
        unlink(temporary);
    } else unlink(current);
    return -1;
}

static void JsonString(const char *value)
{
    putchar('"');
    for (;*value;value++) {
        unsigned char c=(unsigned char)*value;
        if (c=='"' || c=='\\') putchar('\\');
        if (c>=32) putchar(c);
    }
    putchar('"');
}

static void Current(char *name,size_t capacity)
{
    char path[512],target[512];
    snprintf(path,sizeof(path),"%s/current",MICROFX_APPS_DIR);
    ssize_t length=readlink(path,target,sizeof(target)-1);
    if (length<0) { name[0]='\0'; return; }
    target[length]='\0';
    const char *base=strrchr(target,'/'); base=base?base+1:target;
    snprintf(name,capacity,"%s",base);
}

static void Activation(char *token,size_t tokenCapacity,char *project,size_t projectCapacity,
                       char *state,size_t stateCapacity,char *detail,size_t detailCapacity)
{
    token[0]=project[0]=state[0]=detail[0]='\0';
    FILE *file=fopen(MICROFX_RELOAD_STATUS,"r");
    if (!file) return;
    char line[512]={0};
    if (fgets(line,sizeof(line),file)) {
        char *cursor=line,*tab;
        char *parts[4]={0};
        for (int i=0;i<3;i++) {
            parts[i]=cursor; tab=strchr(cursor,'\t');
            if (!tab) { fclose(file); return; }
            *tab='\0'; cursor=tab+1;
        }
        parts[3]=cursor; parts[3][strcspn(parts[3],"\r\n")]='\0';
        snprintf(token,tokenCapacity,"%s",parts[0]);
        snprintf(project,projectCapacity,"%s",parts[1]);
        snprintf(state,stateCapacity,"%s",parts[2]);
        snprintf(detail,detailCapacity,"%s",parts[3]);
    }
    fclose(file);
}

static void StatusField(const char *path,const char *name,char *value,size_t capacity)
{
    value[0]='\0';
    FILE *file=fopen(path,"r");
    if (!file) return;
    char line[512]; size_t length=strlen(name);
    while (fgets(line,sizeof(line),file)) {
        if (!strncmp(line,name,length) && line[length]=='\t') {
            snprintf(value,capacity,"%s",line+length+1);
            value[strcspn(value,"\r\n")]='\0';
            break;
        }
    }
    fclose(file);
}

static void NetworkField(const char *name,char *value,size_t capacity)
{
    StatusField(MICROFX_NETWORK_STATUS,name,value,capacity);
}

static void ProvisionField(const char *name,char *value,size_t capacity)
{
    StatusField(MICROFX_PROVISION_STATUS,name,value,capacity);
}

static void PeerId(char *value,size_t capacity)
{
    value[0]='\0';
    FILE *file=fopen(MICROFX_PEER_ID_FILE,"r");
    if (file) {
        if (!fgets(value,(int)capacity,file)) value[0]='\0';
        fclose(file);
        value[strcspn(value,"\r\n")]='\0';
    }
    if (!ValidName(value)) snprintf(value,capacity,"microfx-demo");
}

static void Status(void)
{
    char projectsPath[512],active[65],peerId[65],token[129],project[65],state[32],detail[256];
    char networkState[32],networkDetail[128],ssid[64],address[64],signal[32],bitrate[32],txpower[32];
    char setupState[32],setupRadio[8],setupApMode[8],setupLink[8],setupAddress[8],setupPortal[8],setupBeacon[8],setupFailures[16];
    snprintf(projectsPath,sizeof(projectsPath),"%s/projects",MICROFX_APPS_DIR);
    Current(active,sizeof(active));
    PeerId(peerId,sizeof(peerId));
    Activation(token,sizeof(token),project,sizeof(project),state,sizeof(state),detail,sizeof(detail));
    NetworkField("state",networkState,sizeof(networkState)); NetworkField("detail",networkDetail,sizeof(networkDetail));
    NetworkField("ssid",ssid,sizeof(ssid)); NetworkField("address",address,sizeof(address));
    NetworkField("signal",signal,sizeof(signal)); NetworkField("bitrate",bitrate,sizeof(bitrate));
    NetworkField("txpower",txpower,sizeof(txpower));
    ProvisionField("state",setupState,sizeof(setupState)); ProvisionField("radio",setupRadio,sizeof(setupRadio));
    ProvisionField("ap_mode",setupApMode,sizeof(setupApMode)); ProvisionField("link",setupLink,sizeof(setupLink));
    ProvisionField("address",setupAddress,sizeof(setupAddress)); ProvisionField("portal",setupPortal,sizeof(setupPortal));
    ProvisionField("beacon",setupBeacon,sizeof(setupBeacon));
    ProvisionField("failures",setupFailures,sizeof(setupFailures));
    printf("Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n{\"ok\":true,\"active\":");
    JsonString(active); fputs(",\"peerId\":",stdout); JsonString(peerId);
    fputs(",\"activation\":{\"token\":",stdout); JsonString(token);
    fputs(",\"project\":",stdout); JsonString(project); fputs(",\"state\":",stdout); JsonString(state);
    fputs(",\"detail\":",stdout); JsonString(detail); fputs("},\"network\":{\"state\":",stdout); JsonString(networkState);
    fputs(",\"detail\":",stdout); JsonString(networkDetail); fputs(",\"ssid\":",stdout); JsonString(ssid);
    fputs(",\"address\":",stdout); JsonString(address); fputs(",\"signal\":",stdout); JsonString(signal);
    fputs(",\"bitrate\":",stdout); JsonString(bitrate); fputs(",\"txpower\":",stdout); JsonString(txpower);
    fputs("},\"setup\":{\"state\":",stdout); JsonString(setupState);
    fputs(",\"radio\":",stdout); JsonString(setupRadio); fputs(",\"apMode\":",stdout); JsonString(setupApMode);
    fputs(",\"link\":",stdout); JsonString(setupLink); fputs(",\"address\":",stdout); JsonString(setupAddress);
    fputs(",\"portal\":",stdout); JsonString(setupPortal); fputs(",\"beacon\":",stdout); JsonString(setupBeacon);
    fputs(",\"failures\":",stdout); JsonString(setupFailures);
    fputs("},\"projects\":[",stdout);
    DIR *directory=opendir(projectsPath); int first=1;
    if (directory) {
        struct dirent *entry;
        while ((entry=readdir(directory))) {
            if (!ValidName(entry->d_name) || !ProjectExists(entry->d_name)) continue;
            char title[97]; ProjectTitle(entry->d_name,title,sizeof(title));
            if (!first) putchar(','); first=0;
            fputs("{\"id\":",stdout); JsonString(entry->d_name);
            fputs(",\"title\":",stdout); JsonString(title); putchar('}');
        }
        closedir(directory);
    }
    fputs("]}\n",stdout);
}

static void Reply(int ok,const char *message,const char *token)
{
    printf("Status: %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n\r\n{\"ok\":%s,\"message\":",
           ok?"200 OK":"400 Bad Request",ok?"true":"false");
    JsonString(message); fputs(",\"activation\":",stdout); JsonString(token?token:""); fputs("}\n",stdout);
}

int main(void)
{
    const char *method=getenv("REQUEST_METHOD");
    if (!method || strcmp(method,"GET")==0) { Status(); return 0; }
    char body[256]={0},action[32]={0},project[65]={0};
    long length=strtol(getenv("CONTENT_LENGTH")?getenv("CONTENT_LENGTH"):"0",NULL,10);
    if (length<1 || length>255 || fread(body,1,(size_t)length,stdin)!=(size_t)length) {
        Reply(0,"invalid request",""); return 1;
    }
    for (char *field=strtok(body,"&");field;field=strtok(NULL,"&")) {
        if (!strncmp(field,"action=",7)) snprintf(action,sizeof(action),"%s",field+7);
        if (!strncmp(field,"project=",8)) snprintf(project,sizeof(project),"%s",field+8);
    }
    char token[129]={0};
    if (!strcmp(action,"restart")) Current(project,sizeof(project));
    if ((!strcmp(action,"activate") || !strcmp(action,"restart")) &&
        Activate(project,token,sizeof(token))==0) Reply(1,"renderer activation requested",token);
    else { Reply(0,"invalid action or project",""); return 1; }
    return 0;
}
