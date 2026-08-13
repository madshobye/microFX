#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MICROFX_CONFIG_DIR "/tmp/microfx-provision-test"
#define MICROFX_WIFI_RELOAD_SIGNAL "/tmp/microfx-provision-test.reload"
#define main microfx_provision_cgi_main
#include "../src/save.c"
#undef main

static char *ReadFile(const char *path)
{
    FILE *file=fopen(path,"rb"); assert(file);
    assert(fseek(file,0,SEEK_END)==0); long size=ftell(file); assert(size>=0); rewind(file);
    char *content=calloc((size_t)size+1,1); assert(content);
    assert(fread(content,1,(size_t)size,file)==(size_t)size); fclose(file);
    return content;
}

int main(void)
{
    unlink(MICROFX_CONFIG_DIR "/wpa_supplicant.conf");
    unlink(MICROFX_CONFIG_DIR "/wpa_supplicant.new");
    unlink(MICROFX_CONFIG_DIR "/peer-id");
    unlink(MICROFX_CONFIG_DIR "/peer-id.new");
    rmdir(MICROFX_CONFIG_DIR);

    assert(ValidPeerId("microfx-demo_1.dk"));
    assert(!ValidPeerId("bad id"));
    assert(!ValidPeerId("bad/id"));
    assert(SaveNetwork("First Network","first\\\"password") == 0);
    assert(SaveNetwork("Second Network","second-password") == 0);
    assert(SavePeerId("microfx-demo_1.dk") == 0);

    char *networks=ReadFile(MICROFX_CONFIG_DIR "/wpa_supplicant.conf");
    assert(strstr(networks,"ssid=\"First Network\"") != NULL);
    assert(strstr(networks,"psk=\"first\\\\\\\"password\"") != NULL);
    assert(strstr(networks,"ssid=\"Second Network\"") != NULL);
    assert(strstr(networks,"psk=\"second-password\"") != NULL);
    const char *first=strstr(networks,"First Network");
    const char *second=strstr(networks,"Second Network");
    assert(first && second && first < second);
    free(networks);

    char *peer=ReadFile(MICROFX_CONFIG_DIR "/peer-id");
    assert(strcmp(peer,"microfx-demo_1.dk\n") == 0); free(peer);

    unlink(MICROFX_CONFIG_DIR "/wpa_supplicant.conf");
    unlink(MICROFX_CONFIG_DIR "/peer-id");
    rmdir(MICROFX_CONFIG_DIR);
    puts("provisioning tests passed");
    return 0;
}
