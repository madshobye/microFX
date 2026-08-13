#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_BODY 2048
#ifndef MICROFX_CONFIG_DIR
#define MICROFX_CONFIG_DIR "/data/config"
#endif
#ifndef MICROFX_WIFI_RELOAD_SIGNAL
#define MICROFX_WIFI_RELOAD_SIGNAL "/run/microfx-wifi-reload"
#endif

static int Hex(char c)
{
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    if (c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
}

static void Decode(char *out, size_t capacity, const char *in, size_t length)
{
    size_t cursor=0;
    for (size_t i=0; i<length && cursor+1<capacity; i++) {
        if (in[i]=='+') out[cursor++]=' ';
        else if (in[i]=='%' && i+2<length && Hex(in[i+1])>=0 && Hex(in[i+2])>=0) {
            out[cursor++]=(char)(Hex(in[i+1])*16+Hex(in[i+2])); i+=2;
        } else out[cursor++]=in[i];
    }
    out[cursor]='\0';
}

static void Field(char *out, size_t capacity, const char *body, const char *name)
{
    size_t nameLength=strlen(name);
    const char *part=body;
    while (*part) {
        const char *end=strchr(part,'&'); if (!end) end=part+strlen(part);
        if ((size_t)(end-part)>nameLength && !strncmp(part,name,nameLength) && part[nameLength]=='=') {
            Decode(out,capacity,part+nameLength+1,(size_t)(end-part-nameLength-1)); return;
        }
        part=(*end=='&')?end+1:end;
    }
    out[0]='\0';
}

static int ValidText(const char *value, size_t minimum, size_t maximum)
{
    size_t length=strlen(value);
    if (length<minimum || length>maximum) return 0;
    for (size_t i=0;i<length;i++) if ((unsigned char)value[i]<32) return 0;
    return 1;
}

static int ValidPeerId(const char *value)
{
    size_t length=strlen(value);
    if (length<1 || length>64) return 0;
    for (size_t i=0;i<length;i++) {
        unsigned char c=(unsigned char)value[i];
        if (!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
              (c>='0' && c<='9') || c=='.' || c=='_' || c=='-')) return 0;
    }
    return 1;
}

static void Quote(FILE *file, const char *value)
{
    fputc('"',file);
    for (;*value;value++) { if (*value=='"' || *value=='\\') fputc('\\',file); fputc(*value,file); }
    fputc('"',file);
}

static int SaveNetwork(const char *ssid, const char *password)
{
    mkdir(MICROFX_CONFIG_DIR,0700);
    int input=open(MICROFX_CONFIG_DIR "/wpa_supplicant.conf",O_RDONLY);
    FILE *output=fopen(MICROFX_CONFIG_DIR "/wpa_supplicant.new","w");
    if (!output) return -1;
    if (input>=0) {
        char buffer[1024]; ssize_t count;
        while ((count=read(input,buffer,sizeof(buffer)))>0) fwrite(buffer,1,(size_t)count,output);
        close(input);
    } else fputs("ctrl_interface=/run/wpa_supplicant\nupdate_config=0\ncountry=DK\n",output);
    fputs("\nnetwork={\n    ssid=",output); Quote(output,ssid);
    fputs("\n    psk=",output); Quote(output,password);
    fputs("\n    key_mgmt=WPA-PSK\n}\n",output);
    if (fclose(output) || chmod(MICROFX_CONFIG_DIR "/wpa_supplicant.new",0600) ||
        rename(MICROFX_CONFIG_DIR "/wpa_supplicant.new",MICROFX_CONFIG_DIR "/wpa_supplicant.conf")) return -1;
    return 0;
}

static int SavePeerId(const char *peerId)
{
    FILE *file=fopen(MICROFX_CONFIG_DIR "/peer-id.new","w");
    if (!file) return -1;
    fprintf(file,"%s\n",peerId);
    if (fclose(file) || chmod(MICROFX_CONFIG_DIR "/peer-id.new",0600) ||
        rename(MICROFX_CONFIG_DIR "/peer-id.new",MICROFX_CONFIG_DIR "/peer-id")) return -1;
    return 0;
}

int main(void)
{
    char body[MAX_BODY+1]={0},ssid[33],password[64],peerId[65];
    long length=strtol(getenv("CONTENT_LENGTH")?getenv("CONTENT_LENGTH"):"0",NULL,10);
    if (length<1 || length>MAX_BODY || fread(body,1,(size_t)length,stdin)!=(size_t)length) goto invalid;
    Field(ssid,sizeof(ssid),body,"ssid"); Field(password,sizeof(password),body,"password");
    Field(peerId,sizeof(peerId),body,"peer_id");
    if (!ValidText(ssid,1,32) || !ValidText(password,8,63) || !ValidPeerId(peerId)) goto invalid;
    if (SaveNetwork(ssid,password) || SavePeerId(peerId)) {
        printf("Status: 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nSave failed: %s\n",strerror(errno));
        return 1;
    }
    int signal=open(MICROFX_WIFI_RELOAD_SIGNAL,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if (signal>=0) close(signal);
    printf("Content-Type: text/html\r\n\r\n<!doctype html><meta name=viewport content='width=device-width'><title>Setup saved</title><h1>Saved</h1><p>The device will now try the saved network. You can add another network by returning to <a href='/'>setup</a>.</p>");
    return 0;
invalid:
    printf("Status: 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nSSID, 8-63 character password, and a Peer ID using letters, digits, dot, underscore or hyphen are required.\n");
    return 1;
}
