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

static int LineEquals(const char *line, const char *value)
{
    while (isspace((unsigned char)*line)) line++;
    size_t length=strlen(value);
    if (strncmp(line,value,length)) return 0;
    line+=length;
    while (isspace((unsigned char)*line)) line++;
    return *line=='\0';
}

static int NetworkSsid(const char *line, char *value, size_t capacity)
{
    while (isspace((unsigned char)*line)) line++;
    if (strncmp(line,"ssid",4)) return 0;
    line+=4;
    while (isspace((unsigned char)*line)) line++;
    if (*line++!='=') return 0;
    while (isspace((unsigned char)*line)) line++;
    if (*line++!='"') return 0;
    size_t cursor=0;
    while (*line && *line!='"') {
        unsigned char c=(unsigned char)*line++;
        if (c=='\\') {
            c=(unsigned char)*line++;
            if (c!='"' && c!='\\') return 0;
        }
        if (c<32 || cursor+1>=capacity) return 0;
        value[cursor++]=(char)c;
    }
    if (*line++!='"') return 0;
    while (isspace((unsigned char)*line)) line++;
    if (*line!='\0') return 0;
    value[cursor]='\0';
    return 1;
}

static int CopyOtherNetworks(FILE *input, FILE *output, const char *ssid)
{
    char line[1024], block[8192];
    size_t used=0;
    int inNetwork=0, matching=0;
    while (fgets(line,sizeof(line),input)) {
        if (!strchr(line,'\n') && !feof(input)) return -1;
        if (!inNetwork) {
            char normalized[1024];
            snprintf(normalized,sizeof(normalized),"%s",line);
            normalized[strcspn(normalized,"\r\n")]='\0';
            if (LineEquals(normalized,"network={")) {
                inNetwork=1; matching=0; used=0;
            } else if (fputs(line,output)==EOF) return -1;
        }
        if (inNetwork) {
            size_t length=strlen(line);
            if (length>=sizeof(block)-used) return -1;
            memcpy(block+used,line,length); used+=length; block[used]='\0';
            char normalized[1024], parsed[33];
            snprintf(normalized,sizeof(normalized),"%s",line);
            normalized[strcspn(normalized,"\r\n")]='\0';
            if (NetworkSsid(normalized,parsed,sizeof(parsed)) && !strcmp(parsed,ssid)) matching=1;
            if (LineEquals(normalized,"}")) {
                if (!matching && fwrite(block,1,used,output)!=used) return -1;
                inNetwork=0; used=0;
            }
        }
    }
    // Preserve a malformed trailing block instead of silently discarding user
    // configuration. wpa_supplicant will report the original syntax problem.
    if (inNetwork && fwrite(block,1,used,output)!=used) return -1;
    return ferror(input) || ferror(output) ? -1 : 0;
}

static int SaveNetwork(const char *ssid, const char *password)
{
    mkdir(MICROFX_CONFIG_DIR,0700);
    FILE *input=fopen(MICROFX_CONFIG_DIR "/wpa_supplicant.conf","r");
    FILE *output=fopen(MICROFX_CONFIG_DIR "/wpa_supplicant.new","w");
    if (!output) { if (input) fclose(input); return -1; }
    if (input) {
        if (CopyOtherNetworks(input,output,ssid)) { fclose(input); fclose(output); unlink(MICROFX_CONFIG_DIR "/wpa_supplicant.new"); return -1; }
        if (fclose(input)) { fclose(output); unlink(MICROFX_CONFIG_DIR "/wpa_supplicant.new"); return -1; }
    } else if (fputs("ctrl_interface=/run/wpa_supplicant\nupdate_config=0\ncountry=DK\n",output)==EOF) {
        fclose(output); unlink(MICROFX_CONFIG_DIR "/wpa_supplicant.new"); return -1;
    }
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
