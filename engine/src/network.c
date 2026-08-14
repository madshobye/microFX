#include "microfx/network.h"
#include <curl/curl.h>
#include <libwebsockets.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { HTTP_LIMIT=8, SOCKET_LIMIT=16, WEBSOCKET_LIMIT=4,
       WEBSOCKET_QUEUE_LIMIT=32, WEBSOCKET_MESSAGES_PER_PUMP=8,
       BODY_LIMIT=256*1024, IO_LIMIT=64*1024, WEBSOCKET_HANDLE_BASE=1000 };
typedef enum { NET_UNUSED, NET_UDP, NET_TCP_CONNECTING, NET_TCP, NET_TCP_LISTENER } NetKind;

typedef struct {
    CURL *easy;
    struct curl_slist *headers;
    char *body;
    size_t size;
    size_t capacity;
    bool overflow;
    char error[CURL_ERROR_SIZE];
    JSValue resolve;
    JSValue reject;
} HttpRequest;

typedef struct {
    int fd;
    NetKind kind;
    JSValue callbacks[5];
} NetSocket;

typedef struct {
    struct MicroFxNetwork *network;
    struct lws *wsi;
    JSValue callbacks[4];
    uint8_t *incoming;
    size_t incomingSize;
    uint8_t *outgoing;
    size_t outgoingSize;
    uint8_t *messages[WEBSOCKET_QUEUE_LIMIT];
    size_t messageSizes[WEBSOCKET_QUEUE_LIMIT];
    int messageHead;
    int messageCount;
    bool active;
    bool loggedFirstMessage;
} WebSocket;

struct MicroFxNetwork {
    JSContext *context;
    CURLM *multi;
    HttpRequest http[HTTP_LIMIT];
    NetSocket sockets[SOCKET_LIMIT];
    struct lws_context *websocketContext;
    WebSocket websockets[WEBSOCKET_LIMIT];
    bool destroying;
};

enum { EVENT_OPEN, EVENT_DATA, EVENT_CLOSE, EVENT_ERROR, EVENT_CONNECTION };

static int Bytes(JSContext *ctx,JSValueConst value,const uint8_t **bytes,
                 size_t *length,const char **owned);

static bool EmitWebSocket(WebSocket *socket,int event,int argc,JSValue *argv)
{
    if(!socket||!socket->network||socket->network->destroying)return true;
    JSContext *ctx=socket->network->context;
    JSValue callback=socket->callbacks[event];
    if(!JS_IsFunction(ctx,callback))return true;
    JSValue result=JS_Call(ctx,callback,JS_UNDEFINED,argc,argv);
    if(JS_IsException(result)){JS_FreeValue(ctx,result);return false;}
    JS_FreeValue(ctx,result);return true;
}

static void ResetWebSocket(WebSocket *socket)
{
    if(!socket||!socket->active)return;
    JSContext *ctx=socket->network->context;
    for(int event=0;event<4;event++){
        JS_FreeValue(ctx,socket->callbacks[event]);socket->callbacks[event]=JS_UNDEFINED;
    }
    free(socket->incoming);free(socket->outgoing);
    for(int i=0;i<socket->messageCount;i++){
        int index=(socket->messageHead+i)%WEBSOCKET_QUEUE_LIMIT;
        free(socket->messages[index]);socket->messages[index]=NULL;
    }
    socket->incoming=NULL;socket->outgoing=NULL;
    socket->incomingSize=0;socket->outgoingSize=0;socket->messageHead=0;
    socket->messageCount=0;socket->loggedFirstMessage=false;
    socket->wsi=NULL;socket->active=false;
}

static int WebSocketCallback(struct lws *wsi,enum lws_callback_reasons reason,
                             void *user,void *data,size_t length)
{
    (void)user;WebSocket *socket=(WebSocket *)lws_wsi_user(wsi);
    switch(reason){
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        fprintf(stderr,"MICROFX_NET websocket connected\n");
        if(socket&&!EmitWebSocket(socket,EVENT_OPEN,0,NULL))return -1;
        break;
    case LWS_CALLBACK_CLIENT_RECEIVE:
        if(!socket||length>BODY_LIMIT-socket->incomingSize)return -1;
        if(length){
            uint8_t *next=realloc(socket->incoming,socket->incomingSize+length);
            if(!next)return -1;
            socket->incoming=next;memcpy(next+socket->incomingSize,data,length);
            socket->incomingSize+=length;
        }
        if(lws_is_final_fragment(wsi)&&lws_remaining_packet_payload(wsi)==0){
            if(socket->messageCount==WEBSOCKET_QUEUE_LIMIT){
                free(socket->messages[socket->messageHead]);
                socket->messages[socket->messageHead]=NULL;
                socket->messageHead=(socket->messageHead+1)%WEBSOCKET_QUEUE_LIMIT;
                socket->messageCount--;
            }
            int index=(socket->messageHead+socket->messageCount)%WEBSOCKET_QUEUE_LIMIT;
            socket->messages[index]=socket->incoming;
            socket->messageSizes[index]=socket->incomingSize;
            socket->messageCount++;
            socket->incoming=NULL;socket->incomingSize=0;
            if(!socket->loggedFirstMessage){
                fprintf(stderr,"MICROFX_NET websocket first_message bytes=%zu\n",
                        socket->messageSizes[index]);socket->loggedFirstMessage=true;
            }
            // Keep a busy stream from making one lws_service() call drain an
            // unbounded number of messages before the renderer gets control.
            lws_rx_flow_control(wsi,0);
        }
        break;
    case LWS_CALLBACK_CLIENT_WRITEABLE:
        if(socket&&socket->outgoing){
            int written=lws_write(wsi,socket->outgoing+LWS_PRE,socket->outgoingSize,
                                  LWS_WRITE_TEXT);
            if(written<(int)socket->outgoingSize)return -1;
            fprintf(stderr,"MICROFX_NET websocket message_sent bytes=%zu\n",
                    socket->outgoingSize);
            free(socket->outgoing);socket->outgoing=NULL;socket->outgoingSize=0;
        }
        break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        if(socket){
            fprintf(stderr,"MICROFX_NET websocket error=%s\n",
                    data?(const char *)data:"connection failed");
            JSContext *ctx=socket->network->context;
            JSValue value=JS_NewString(ctx,data?(const char *)data:"WebSocket connection failed");
            bool ok=EmitWebSocket(socket,EVENT_ERROR,1,&value);JS_FreeValue(ctx,value);
            ResetWebSocket(socket);if(!ok)return -1;
        }
        break;
    case LWS_CALLBACK_CLIENT_CLOSED:
        if(socket){bool ok=EmitWebSocket(socket,EVENT_CLOSE,0,NULL);ResetWebSocket(socket);if(!ok)return -1;}
        break;
    default:break;
    }
    return 0;
}

static const struct lws_protocols WebSocketProtocols[]={
    {"microfx-websocket",WebSocketCallback,0,IO_LIMIT,0,NULL,0},
    LWS_PROTOCOL_LIST_TERM
};

static MicroFxNetwork *FromData(JSContext *ctx, JSValue *data)
{
    int64_t raw=0;
    if(JS_ToInt64(ctx,&raw,data[0]))return NULL;
    return (MicroFxNetwork *)(intptr_t)raw;
}

static JSValue Bind(MicroFxNetwork *network, JSCFunctionData *function, int argc)
{
    JSValue pointer=JS_NewInt64(network->context,(int64_t)(intptr_t)network);
    JSValue result=JS_NewCFunctionData(network->context,function,argc,0,1,&pointer);
    JS_FreeValue(network->context,pointer);
    return result;
}

static size_t WriteBody(char *data,size_t size,size_t count,void *opaque)
{
    HttpRequest *request=opaque;
    size_t bytes=size*count;
    if(bytes>BODY_LIMIT-request->size){request->overflow=true;return 0;}
    size_t needed=request->size+bytes+1;
    if(needed>request->capacity){
        size_t capacity=request->capacity?request->capacity*2:4096;
        while(capacity<needed)capacity*=2;
        if(capacity>BODY_LIMIT+1)capacity=BODY_LIMIT+1;
        char *body=realloc(request->body,capacity);
        if(!body)return 0;
        request->body=body;request->capacity=capacity;
    }
    memcpy(request->body+request->size,data,bytes);
    request->size+=bytes;request->body[request->size]='\0';
    return bytes;
}

static JSValue Fetch(JSContext *ctx,JSValueConst thisValue,int argc,
                     JSValueConst *argv,int magic,JSValue *data)
{
    (void)thisValue;(void)magic;
    MicroFxNetwork *network=FromData(ctx,data);
    if(!network||argc<1||argc>2)return JS_ThrowTypeError(ctx,"fetch(url, headers) requires a URL and optional headers");
    const char *url=JS_ToCString(ctx,argv[0]);
    if(!url)return JS_EXCEPTION;
    if(strncmp(url,"http://",7)!=0&&strncmp(url,"https://",8)!=0){
        JS_FreeCString(ctx,url);
        return JS_ThrowTypeError(ctx,"fetch URL must use http:// or https://");
    }
    struct curl_slist *headers=NULL;
    if(argc==2&&!JS_IsUndefined(argv[1])){
        const char *lines=JS_ToCString(ctx,argv[1]);
        if(!lines){JS_FreeCString(ctx,url);return JS_EXCEPTION;}
        size_t length=strlen(lines);
        if(length>8192||strchr(lines,'\r')){
            JS_FreeCString(ctx,lines);JS_FreeCString(ctx,url);
            return JS_ThrowRangeError(ctx,"fetch headers exceed 8 KiB or contain a carriage return");
        }
        char *copy=strdup(lines);JS_FreeCString(ctx,lines);
        if(!copy){JS_FreeCString(ctx,url);return JS_ThrowOutOfMemory(ctx);}
        char *cursor=NULL;char *line=strtok_r(copy,"\n",&cursor);int count=0;
        while(line){
            if(++count>32||line[0]=='\0'||line[0]==':'||!strchr(line,':')){
                free(copy);curl_slist_free_all(headers);JS_FreeCString(ctx,url);
                return JS_ThrowTypeError(ctx,"fetch headers are invalid or exceed 32 fields");
            }
            struct curl_slist *next=curl_slist_append(headers,line);
            if(!next){
                free(copy);curl_slist_free_all(headers);JS_FreeCString(ctx,url);
                return JS_ThrowOutOfMemory(ctx);
            }
            headers=next;line=strtok_r(NULL,"\n",&cursor);
        }
        free(copy);
    }
    HttpRequest *request=NULL;
    for(int i=0;i<HTTP_LIMIT;i++)if(!network->http[i].easy){request=&network->http[i];break;}
    if(!request){curl_slist_free_all(headers);JS_FreeCString(ctx,url);return JS_ThrowRangeError(ctx,"maximum 8 HTTP requests in flight");}
    JSValue functions[2];
    JSValue promise=JS_NewPromiseCapability(ctx,functions);
    if(JS_IsException(promise)){curl_slist_free_all(headers);JS_FreeCString(ctx,url);return promise;}
    request->easy=curl_easy_init();
    if(!request->easy){
        JS_FreeValue(ctx,functions[0]);JS_FreeValue(ctx,functions[1]);JS_FreeValue(ctx,promise);
        curl_slist_free_all(headers);JS_FreeCString(ctx,url);
        return JS_ThrowInternalError(ctx,"HTTP initialization failed");
    }
    request->resolve=functions[0];request->reject=functions[1];
    request->headers=headers;
    curl_easy_setopt(request->easy,CURLOPT_URL,url);
    curl_easy_setopt(request->easy,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(request->easy,CURLOPT_MAXREDIRS,5L);
    curl_easy_setopt(request->easy,CURLOPT_CONNECTTIMEOUT_MS,5000L);
    curl_easy_setopt(request->easy,CURLOPT_TIMEOUT_MS,20000L);
    curl_easy_setopt(request->easy,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(request->easy,CURLOPT_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(request->easy,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(request->easy,CURLOPT_USERAGENT,"microFX/1");
    if(request->headers)curl_easy_setopt(request->easy,CURLOPT_HTTPHEADER,request->headers);
    curl_easy_setopt(request->easy,CURLOPT_CAINFO,"/etc/ssl/certs/ca-certificates.crt");
    curl_easy_setopt(request->easy,CURLOPT_WRITEFUNCTION,WriteBody);
    curl_easy_setopt(request->easy,CURLOPT_WRITEDATA,request);
    curl_easy_setopt(request->easy,CURLOPT_PRIVATE,request);
    request->error[0]='\0';
    curl_easy_setopt(request->easy,CURLOPT_ERRORBUFFER,request->error);
    CURLMcode added=curl_multi_add_handle(network->multi,request->easy);
    JS_FreeCString(ctx,url);
    if(added!=CURLM_OK){
        curl_easy_cleanup(request->easy);request->easy=NULL;
        curl_slist_free_all(request->headers);request->headers=NULL;
        JS_FreeValue(ctx,request->resolve);JS_FreeValue(ctx,request->reject);
        JS_FreeValue(ctx,promise);
        return JS_ThrowInternalError(ctx,"HTTP request could not be queued");
    }
    return promise;
}

static int NonBlocking(int fd)
{
    int flags=fcntl(fd,F_GETFL,0);
    return flags<0?-1:fcntl(fd,F_SETFL,flags|O_NONBLOCK);
}

static int AllocateSocket(MicroFxNetwork *network,int fd,NetKind kind)
{
    for(int i=0;i<SOCKET_LIMIT;i++)if(network->sockets[i].kind==NET_UNUSED){
        network->sockets[i].fd=fd;network->sockets[i].kind=kind;return i+1;
    }
    return 0;
}

static NetSocket *SocketFor(MicroFxNetwork *network,int32_t handle)
{
    if(handle<1||handle>SOCKET_LIMIT)return NULL;
    NetSocket *socket=&network->sockets[handle-1];
    return socket->kind==NET_UNUSED?NULL:socket;
}

static WebSocket *WebSocketFor(MicroFxNetwork *network,int32_t handle)
{
    int index=handle-WEBSOCKET_HANDLE_BASE-1;
    if(index<0||index>=WEBSOCKET_LIMIT)return NULL;
    return network->websockets[index].active?&network->websockets[index]:NULL;
}

static JSValue WebSocketConnect(JSContext *ctx,JSValueConst thisValue,int argc,
                                JSValueConst *argv,int magic,JSValue *data)
{
    (void)thisValue;(void)magic;MicroFxNetwork *network=FromData(ctx,data);
    if(!network||argc!=1)return JS_ThrowTypeError(ctx,"websocket.connect(url) requires one URL");
    const char *input=JS_ToCString(ctx,argv[0]);if(!input)return JS_EXCEPTION;
    size_t inputLength=strlen(input);
    if(inputLength<6||inputLength>=2048||
       (strncmp(input,"ws://",5)!=0&&strncmp(input,"wss://",6)!=0)){
        JS_FreeCString(ctx,input);return JS_ThrowTypeError(ctx,"WebSocket URL must use ws:// or wss://");
    }
    char url[2048];memcpy(url,input,inputLength+1);JS_FreeCString(ctx,input);
    const char *protocol=NULL,*address=NULL,*path=NULL;int port=0;
    if(lws_parse_uri(url,&protocol,&address,&port,&path)||!address||!path)
        return JS_ThrowTypeError(ctx,"invalid WebSocket URL");
    WebSocket *socket=NULL;int index=0;
    for(index=0;index<WEBSOCKET_LIMIT;index++)if(!network->websockets[index].active){socket=&network->websockets[index];break;}
    if(!socket)return JS_ThrowRangeError(ctx,"maximum 4 WebSocket connections");
    socket->network=network;socket->active=true;socket->loggedFirstMessage=false;
    for(int event=0;event<4;event++)socket->callbacks[event]=JS_UNDEFINED;
    char fullPath[2048];snprintf(fullPath,sizeof(fullPath),"/%s",path[0]=='/'?path+1:path);
    struct lws_client_connect_info info={0};
    info.context=network->websocketContext;info.address=address;info.port=port;
    info.path=fullPath;info.host=address;info.origin=address;
    info.protocol=WebSocketProtocols[0].name;info.userdata=socket;
    if(strcmp(protocol,"wss")==0)info.ssl_connection=LCCSCF_USE_SSL;
    socket->wsi=lws_client_connect_via_info(&info);
    if(!socket->wsi){ResetWebSocket(socket);return JS_ThrowInternalError(ctx,"WebSocket connection could not be queued");}
    return JS_NewInt32(ctx,WEBSOCKET_HANDLE_BASE+index+1);
}

static JSValue WebSocketSend(JSContext *ctx,JSValueConst thisValue,int argc,
                             JSValueConst *argv,int magic,JSValue *data)
{
    (void)thisValue;(void)magic;MicroFxNetwork *network=FromData(ctx,data);int32_t handle=0;
    if(!network||argc<2||JS_ToInt32(ctx,&handle,argv[0]))
        return JS_ThrowTypeError(ctx,"websocket.send(handle,data)");
    WebSocket *socket=WebSocketFor(network,handle);
    if(!socket||!socket->wsi)return JS_ThrowRangeError(ctx,"WebSocket is closed");
    if(socket->outgoing)return JS_NewInt32(ctx,0);
    const uint8_t *bytes=NULL;size_t length=0;const char *owned=NULL;
    if(Bytes(ctx,argv[1],&bytes,&length,&owned)<0)
        return JS_ThrowTypeError(ctx,"WebSocket data must be a string or ArrayBuffer");
    if(length>IO_LIMIT){if(owned)JS_FreeCString(ctx,owned);return JS_ThrowRangeError(ctx,"WebSocket message exceeds 64 KiB");}
    socket->outgoing=malloc(LWS_PRE+length);
    if(!socket->outgoing){if(owned)JS_FreeCString(ctx,owned);return JS_ThrowOutOfMemory(ctx);}
    memcpy(socket->outgoing+LWS_PRE,bytes,length);socket->outgoingSize=length;
    if(owned)JS_FreeCString(ctx,owned);lws_callback_on_writable(socket->wsi);
    return JS_NewInt32(ctx,(int)length);
}

static bool Resolve(const char *host,int port,int type,bool passive,
                    struct sockaddr_storage *address,socklen_t *length)
{
    char service[16];snprintf(service,sizeof(service),"%d",port);
    struct addrinfo hints={0},*result=NULL;
    hints.ai_family=AF_UNSPEC;hints.ai_socktype=type;
    if(passive)hints.ai_flags=AI_PASSIVE;
    int status=getaddrinfo(host&&host[0]?host:NULL,service,&hints,&result);
    if(status!=0||!result)return false;
    if(result->ai_addrlen>sizeof(*address)){freeaddrinfo(result);return false;}
    memcpy(address,result->ai_addr,result->ai_addrlen);*length=result->ai_addrlen;
    freeaddrinfo(result);return true;
}

static JSValue UdpOpen(JSContext *ctx,JSValueConst thisValue,int argc,
                       JSValueConst *argv,int magic,JSValue *data)
{
    (void)thisValue;(void)magic;MicroFxNetwork *network=FromData(ctx,data);
    int32_t port=0;const char *host=NULL;
    if(!network||argc<2||JS_ToInt32(ctx,&port,argv[1])||port<0||port>65535)
        return JS_ThrowTypeError(ctx,"udp.open(host,port) requires a valid port");
    host=JS_ToCString(ctx,argv[0]);if(!host)return JS_EXCEPTION;
    struct sockaddr_storage address;socklen_t length=0;
    bool resolved=Resolve(host,port,SOCK_DGRAM,true,&address,&length);
    JS_FreeCString(ctx,host);
    if(!resolved)return JS_ThrowInternalError(ctx,"UDP bind address could not be resolved");
    int fd=socket(address.ss_family,SOCK_DGRAM,0);
    int reuse=1;if(fd>=0)setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    if(fd<0||NonBlocking(fd)<0||bind(fd,(struct sockaddr *)&address,length)<0){
        if(fd>=0)close(fd);return JS_ThrowInternalError(ctx,"UDP socket could not be opened");
    }
    int handle=AllocateSocket(network,fd,NET_UDP);
    if(!handle){close(fd);return JS_ThrowRangeError(ctx,"maximum 16 network sockets");}
    return JS_NewInt32(ctx,handle);
}

static JSValue TcpOpen(JSContext *ctx,JSValueConst thisValue,int argc,
                       JSValueConst *argv,int magic,JSValue *data)
{
    (void)thisValue;MicroFxNetwork *network=FromData(ctx,data);int32_t port=0;
    if(!network||argc<2||JS_ToInt32(ctx,&port,argv[1])||port<1||port>65535)
        return JS_ThrowTypeError(ctx,magic?"tcp.listen(host,port) requires a valid port":"tcp.connect(host,port) requires a valid port");
    const char *host=JS_ToCString(ctx,argv[0]);if(!host)return JS_EXCEPTION;
    struct sockaddr_storage address;socklen_t length=0;
    bool resolved=Resolve(host,port,SOCK_STREAM,magic!=0,&address,&length);
    JS_FreeCString(ctx,host);
    if(!resolved)return JS_ThrowInternalError(ctx,"TCP address could not be resolved");
    int fd=socket(address.ss_family,SOCK_STREAM,0);
    int reuse=1;if(fd>=0)setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    if(fd<0||NonBlocking(fd)<0){if(fd>=0)close(fd);return JS_ThrowInternalError(ctx,"TCP socket could not be opened");}
    NetKind kind;
    if(magic){
        if(bind(fd,(struct sockaddr *)&address,length)<0||listen(fd,8)<0){close(fd);return JS_ThrowInternalError(ctx,"TCP listener could not bind");}
        kind=NET_TCP_LISTENER;
    }else{
        int status=connect(fd,(struct sockaddr *)&address,length);
        if(status<0&&errno!=EINPROGRESS){close(fd);return JS_ThrowInternalError(ctx,"TCP connection failed");}
        kind=status==0?NET_TCP:NET_TCP_CONNECTING;
    }
    int handle=AllocateSocket(network,fd,kind);
    if(!handle){close(fd);return JS_ThrowRangeError(ctx,"maximum 16 network sockets");}
    return JS_NewInt32(ctx,handle);
}

static int Bytes(JSContext *ctx,JSValueConst value,const uint8_t **bytes,size_t *length,
                 const char **owned)
{
    *owned=NULL;
    if(JS_IsString(value)){
        *owned=JS_ToCStringLen(ctx,length,value);*bytes=(const uint8_t *)*owned;
        return *owned?0:-1;
    }
    *bytes=JS_GetArrayBuffer(ctx,length,value);
    return *bytes?0:-1;
}

static JSValue NetSend(JSContext *ctx,JSValueConst thisValue,int argc,
                       JSValueConst *argv,int magic,JSValue *data)
{
    (void)thisValue;(void)magic;MicroFxNetwork *network=FromData(ctx,data);int32_t handle=0;
    if(!network||argc<2||JS_ToInt32(ctx,&handle,argv[0]))return JS_ThrowTypeError(ctx,"socket.send(handle,data[,host,port])");
    NetSocket *socket=SocketFor(network,handle);
    if(!socket)return JS_ThrowRangeError(ctx,"network socket is closed");
    const uint8_t *bytes=NULL;size_t length=0;const char *owned=NULL;
    if(Bytes(ctx,argv[1],&bytes,&length,&owned)<0)return JS_ThrowTypeError(ctx,"network data must be a string or ArrayBuffer");
    ssize_t sent=-1;
    if(socket->kind==NET_UDP){
        int32_t port=0;if(argc<4||JS_ToInt32(ctx,&port,argv[3])||port<1||port>65535){if(owned)JS_FreeCString(ctx,owned);return JS_ThrowTypeError(ctx,"UDP send requires host and port");}
        const char *host=JS_ToCString(ctx,argv[2]);if(!host){if(owned)JS_FreeCString(ctx,owned);return JS_EXCEPTION;}
        struct sockaddr_storage address;socklen_t addressLength=0;
        bool resolved=Resolve(host,port,SOCK_DGRAM,false,&address,&addressLength);
        JS_FreeCString(ctx,host);
        if(resolved)sent=sendto(socket->fd,bytes,length,0,(struct sockaddr *)&address,addressLength);
    }else if(socket->kind==NET_TCP){sent=send(socket->fd,bytes,length,MSG_NOSIGNAL);}
    if(owned)JS_FreeCString(ctx,owned);
    if(sent<0){if(errno==EAGAIN||errno==EWOULDBLOCK)return JS_NewInt32(ctx,0);return JS_ThrowInternalError(ctx,"network send failed");}
    return JS_NewInt32(ctx,(int)sent);
}

static void CloseSocket(MicroFxNetwork *network,NetSocket *socket)
{
    if(socket->fd>=0)close(socket->fd);
    for(int event=0;event<5;event++){JS_FreeValue(network->context,socket->callbacks[event]);socket->callbacks[event]=JS_UNDEFINED;}
    socket->fd=-1;socket->kind=NET_UNUSED;
}

static JSValue NetClose(JSContext *ctx,JSValueConst thisValue,int argc,
                        JSValueConst *argv,int magic,JSValue *data)
{
    (void)thisValue;(void)magic;MicroFxNetwork *network=FromData(ctx,data);int32_t handle=0;
    if(!network||argc<1||JS_ToInt32(ctx,&handle,argv[0]))return JS_ThrowTypeError(ctx,"socket.close(handle)");
    WebSocket *websocket=WebSocketFor(network,handle);
    if(websocket){
        if(websocket->wsi)lws_set_timeout(websocket->wsi,PENDING_TIMEOUT_CLOSE_SEND,1);
        else ResetWebSocket(websocket);
        return JS_UNDEFINED;
    }
    NetSocket *socket=SocketFor(network,handle);if(socket)CloseSocket(network,socket);
    return JS_UNDEFINED;
}

static JSValue NetOn(JSContext *ctx,JSValueConst thisValue,int argc,
                     JSValueConst *argv,int magic,JSValue *data)
{
    (void)thisValue;(void)magic;MicroFxNetwork *network=FromData(ctx,data);int32_t handle=0,event=0;
    if(!network||argc<3||JS_ToInt32(ctx,&handle,argv[0])||JS_ToInt32(ctx,&event,argv[1])||event<0||event>4||!JS_IsFunction(ctx,argv[2]))
        return JS_ThrowTypeError(ctx,"socket event registration is invalid");
    WebSocket *websocket=WebSocketFor(network,handle);
    if(websocket){
        if(event>EVENT_ERROR)return JS_ThrowTypeError(ctx,"WebSocket event registration is invalid");
        JS_FreeValue(ctx,websocket->callbacks[event]);
        websocket->callbacks[event]=JS_DupValue(ctx,argv[2]);return JS_UNDEFINED;
    }
    NetSocket *socket=SocketFor(network,handle);if(!socket)return JS_ThrowRangeError(ctx,"network socket is closed");
    JS_FreeValue(ctx,socket->callbacks[event]);socket->callbacks[event]=JS_DupValue(ctx,argv[2]);
    if(event==EVENT_OPEN&&socket->kind==NET_TCP){
        JSValue result=JS_Call(ctx,socket->callbacks[event],JS_UNDEFINED,0,NULL);
        if(JS_IsException(result))return result;
        JS_FreeValue(ctx,result);
    }
    return JS_UNDEFINED;
}

static bool Emit(MicroFxNetwork *network,NetSocket *socket,int event,int argc,JSValue *argv)
{
    JSValue callback=socket->callbacks[event];
    if(!JS_IsFunction(network->context,callback))return true;
    JSValue result=JS_Call(network->context,callback,JS_UNDEFINED,argc,argv);
    if(JS_IsException(result)){JS_FreeValue(network->context,result);return false;}
    JS_FreeValue(network->context,result);return true;
}

static void AddressObject(JSContext *ctx,struct sockaddr_storage *address,
                          char *host,size_t hostSize,int *port)
{
    void *source=NULL;
    if(address->ss_family==AF_INET){struct sockaddr_in *v4=(struct sockaddr_in *)address;source=&v4->sin_addr;*port=ntohs(v4->sin_port);}
    else {struct sockaddr_in6 *v6=(struct sockaddr_in6 *)address;source=&v6->sin6_addr;*port=ntohs(v6->sin6_port);}
    if(!inet_ntop(address->ss_family,source,host,(socklen_t)hostSize))snprintf(host,hostSize,"unknown");
    (void)ctx;
}

static bool PumpHttp(MicroFxNetwork *network)
{
    int running=0;CURLMcode status=curl_multi_perform(network->multi,&running);
    if(status!=CURLM_OK)return false;
    int remaining=0;CURLMsg *message;
    while((message=curl_multi_info_read(network->multi,&remaining))){
        if(message->msg!=CURLMSG_DONE)continue;
        HttpRequest *request=NULL;curl_easy_getinfo(message->easy_handle,CURLINFO_PRIVATE,&request);
        long code=0;char *url=NULL;curl_easy_getinfo(message->easy_handle,CURLINFO_RESPONSE_CODE,&code);curl_easy_getinfo(message->easy_handle,CURLINFO_EFFECTIVE_URL,&url);
        JSValue argument;
        JSValue function;
        if(message->data.result==CURLE_OK&&!request->overflow){
            argument=JS_NewObject(network->context);
            JS_SetPropertyStr(network->context,argument,"status",JS_NewInt32(network->context,(int)code));
            JS_SetPropertyStr(network->context,argument,"url",JS_NewString(network->context,url?url:""));
            JS_SetPropertyStr(network->context,argument,"body",JS_NewStringLen(network->context,request->body?request->body:"",request->size));
            JS_SetPropertyStr(network->context,argument,"bodyBytes",
                JS_NewArrayBufferCopy(network->context,
                    (const uint8_t *)(request->body?request->body:""),request->size));
            function=request->resolve;
        }else{
            const char *detail=request->overflow?"HTTP response exceeds 256 KiB":
                               request->error[0]?request->error:curl_easy_strerror(message->data.result);
            fprintf(stderr,"MICROFX_NET fetch failed code=%d detail=%s\n",
                    (int)message->data.result,detail);
            argument=JS_NewError(network->context);
            JS_DefinePropertyValueStr(network->context,argument,"message",
                JS_NewString(network->context,detail),JS_PROP_C_W_E);
            function=request->reject;
        }
        JSValue result=JS_Call(network->context,function,JS_UNDEFINED,1,&argument);
        JS_FreeValue(network->context,argument);
        bool ok=!JS_IsException(result);JS_FreeValue(network->context,result);
        curl_multi_remove_handle(network->multi,request->easy);curl_easy_cleanup(request->easy);
        request->easy=NULL;curl_slist_free_all(request->headers);request->headers=NULL;
        free(request->body);request->body=NULL;request->size=0;request->capacity=0;request->overflow=false;request->error[0]='\0';
        JS_FreeValue(network->context,request->resolve);JS_FreeValue(network->context,request->reject);request->resolve=JS_UNDEFINED;request->reject=JS_UNDEFINED;
        if(!ok)return false;
    }
    return true;
}

static bool PumpSockets(MicroFxNetwork *network)
{
    uint8_t buffer[IO_LIMIT];
    for(int index=0;index<SOCKET_LIMIT;index++){
        NetSocket *socket=&network->sockets[index];if(socket->kind==NET_UNUSED)continue;
        if(socket->kind==NET_TCP_CONNECTING){
            struct pollfd ready={.fd=socket->fd,.events=POLLOUT};
            int polled=poll(&ready,1,0);
            if(polled<=0||!(ready.revents&(POLLOUT|POLLERR|POLLHUP)))continue;
            int error=0;socklen_t length=sizeof(error);
            if(getsockopt(socket->fd,SOL_SOCKET,SO_ERROR,&error,&length)==0&&error==0){socket->kind=NET_TCP;if(!Emit(network,socket,EVENT_OPEN,0,NULL))return false;}
            else if(error!=0&&error!=EINPROGRESS){JSValue value=JS_NewString(network->context,strerror(error));bool ok=Emit(network,socket,EVENT_ERROR,1,&value);JS_FreeValue(network->context,value);CloseSocket(network,socket);if(!ok)return false;}
            continue;
        }
        if(socket->kind==NET_TCP_LISTENER){
            struct sockaddr_storage peer;socklen_t peerLength=sizeof(peer);int accepted=accept(socket->fd,(struct sockaddr *)&peer,&peerLength);
            if(accepted>=0){NonBlocking(accepted);int handle=AllocateSocket(network,accepted,NET_TCP);if(!handle)close(accepted);else {JSValue value=JS_NewInt32(network->context,handle);bool ok=Emit(network,socket,EVENT_CONNECTION,1,&value);JS_FreeValue(network->context,value);if(!ok)return false;}}
            continue;
        }
        struct sockaddr_storage peer;socklen_t peerLength=sizeof(peer);
        ssize_t received=socket->kind==NET_UDP?recvfrom(socket->fd,buffer,sizeof(buffer),0,(struct sockaddr *)&peer,&peerLength):recv(socket->fd,buffer,sizeof(buffer),0);
        if(received>0){
            JSValue args[2];args[0]=JS_NewArrayBufferCopy(network->context,buffer,(size_t)received);int count=1;
            if(socket->kind==NET_UDP){char host[INET6_ADDRSTRLEN];int port=0;AddressObject(network->context,&peer,host,sizeof(host),&port);args[1]=JS_NewObject(network->context);JS_SetPropertyStr(network->context,args[1],"address",JS_NewString(network->context,host));JS_SetPropertyStr(network->context,args[1],"port",JS_NewInt32(network->context,port));count=2;}
            bool ok=Emit(network,socket,EVENT_DATA,count,args);for(int i=0;i<count;i++)JS_FreeValue(network->context,args[i]);if(!ok)return false;
        }else if(received==0&&socket->kind==NET_TCP){bool ok=Emit(network,socket,EVENT_CLOSE,0,NULL);CloseSocket(network,socket);if(!ok)return false;}
        else if(received<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK){JSValue value=JS_NewString(network->context,strerror(errno));bool ok=Emit(network,socket,EVENT_ERROR,1,&value);JS_FreeValue(network->context,value);CloseSocket(network,socket);if(!ok)return false;}
    }
    return true;
}

static bool PumpWebSocketMessages(MicroFxNetwork *network)
{
    int delivered=0;
    for(int socketIndex=0;socketIndex<WEBSOCKET_LIMIT&&
        delivered<WEBSOCKET_MESSAGES_PER_PUMP;socketIndex++){
        WebSocket *socket=&network->websockets[socketIndex];
        while(socket->active&&socket->messageCount>0&&
              delivered<WEBSOCKET_MESSAGES_PER_PUMP){
            int index=socket->messageHead;uint8_t *bytes=socket->messages[index];
            size_t length=socket->messageSizes[index];socket->messages[index]=NULL;
            socket->messageHead=(socket->messageHead+1)%WEBSOCKET_QUEUE_LIMIT;
            socket->messageCount--;delivered++;
            JSValue value=JS_NewArrayBufferCopy(network->context,bytes,length);free(bytes);
            bool ok=EmitWebSocket(socket,EVENT_DATA,1,&value);
            JS_FreeValue(network->context,value);if(!ok)return false;
            if(socket->active&&socket->messageCount==0&&socket->wsi)
                lws_rx_flow_control(socket->wsi,1);
        }
    }
    return true;
}

static bool PumpWebSockets(MicroFxNetwork *network)
{
    bool active=false;
    for(int i=0;i<WEBSOCKET_LIMIT;i++)if(network->websockets[i].active){active=true;break;}
    if(!active)return true;
    // libwebsockets 4.x ignores the legacy timeout argument and may sleep
    // until its next internal timer. Prime its cancellation pipe so service
    // remains a non-blocking part of the render loop.
    lws_cancel_service(network->websocketContext);
    return lws_service(network->websocketContext,0)>=0&&PumpWebSocketMessages(network);
}

MicroFxNetwork *MicroFxNetworkCreate(JSContext *context,JSValueConst fx)
{
    MicroFxNetwork *network=calloc(1,sizeof(*network));if(!network)return NULL;
    network->context=context;
    for(int i=0;i<HTTP_LIMIT;i++){network->http[i].resolve=JS_UNDEFINED;network->http[i].reject=JS_UNDEFINED;}
    for(int i=0;i<SOCKET_LIMIT;i++){network->sockets[i].fd=-1;for(int event=0;event<5;event++)network->sockets[i].callbacks[event]=JS_UNDEFINED;}
    struct lws_context_creation_info websocketInfo={0};
    websocketInfo.port=CONTEXT_PORT_NO_LISTEN;websocketInfo.protocols=WebSocketProtocols;
    websocketInfo.user=network;websocketInfo.options=LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    websocketInfo.mbedtls_client_preload_filepath="/etc/ssl/certs/ca-certificates.crt";
    lws_set_log_level(LLL_ERR|LLL_WARN,NULL);
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK||(network->multi=curl_multi_init())==NULL){free(network);return NULL;}
    network->websocketContext=lws_create_context(&websocketInfo);
    if(!network->websocketContext){curl_multi_cleanup(network->multi);curl_global_cleanup();free(network);return NULL;}
    JS_SetPropertyStr(context,(JSValue)fx,"_netFetch",Bind(network,Fetch,2));
    JS_SetPropertyStr(context,(JSValue)fx,"_netUdpOpen",Bind(network,UdpOpen,2));
    JS_SetPropertyStr(context,(JSValue)fx,"_netTcpConnect",Bind(network,TcpOpen,2));
    JS_SetPropertyStr(context,(JSValue)fx,"_netWebSocketConnect",Bind(network,WebSocketConnect,1));
    JS_SetPropertyStr(context,(JSValue)fx,"_netWebSocketSend",Bind(network,WebSocketSend,2));
    JSValue pointer=JS_NewInt64(context,(int64_t)(intptr_t)network);
    JSValue listener=JS_NewCFunctionData(context,TcpOpen,2,1,1,&pointer);JS_FreeValue(context,pointer);
    JS_SetPropertyStr(context,(JSValue)fx,"_netTcpListen",listener);
    JS_SetPropertyStr(context,(JSValue)fx,"_netSend",Bind(network,NetSend,4));
    JS_SetPropertyStr(context,(JSValue)fx,"_netClose",Bind(network,NetClose,1));
    JS_SetPropertyStr(context,(JSValue)fx,"_netOn",Bind(network,NetOn,3));
    return network;
}

bool MicroFxNetworkPump(MicroFxNetwork *network)
{
    if(!network)return true;
    if(!PumpHttp(network)||!PumpSockets(network)||!PumpWebSockets(network))return false;
    JSContext *context=NULL;int status;
    while((status=JS_ExecutePendingJob(JS_GetRuntime(network->context),&context))>0){}
    return status>=0;
}

void MicroFxNetworkDestroy(MicroFxNetwork *network)
{
    if(!network)return;
    network->destroying=true;
    for(int i=0;i<HTTP_LIMIT;i++)if(network->http[i].easy){curl_multi_remove_handle(network->multi,network->http[i].easy);curl_easy_cleanup(network->http[i].easy);curl_slist_free_all(network->http[i].headers);free(network->http[i].body);JS_FreeValue(network->context,network->http[i].resolve);JS_FreeValue(network->context,network->http[i].reject);}
    for(int i=0;i<SOCKET_LIMIT;i++)if(network->sockets[i].kind!=NET_UNUSED)CloseSocket(network,&network->sockets[i]);
    lws_context_destroy(network->websocketContext);
    for(int i=0;i<WEBSOCKET_LIMIT;i++)ResetWebSocket(&network->websockets[i]);
    curl_multi_cleanup(network->multi);curl_global_cleanup();free(network);
}
