/* 
    Copyright (C) 2020  Terry Sanders

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <bcm_host.h>
#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Broadcom.h>
#include <png.h>

#include "render.h"

#define IMAGE_DECODE "OMX.broadcom.image_decode"
#define VIDEO_DECODE "OMX.broadcom.video_decode"
#define VIDEO_RENDER "OMX.broadcom.video_render"
#define BGRND_SOURCE "OMX.broadcom.source"
#define NULL_SINK    "OMX.broadcom.null_sink"

#define RGB888_TO_RGB565(c)   ((((c) >> 8) & 0xf800 ) | (((c) >> 5) & 0x07e0 ) | (((c) >> 3) & 0x001f ))

#define FATAL(rh,omxcmnd,...)      do {                    \
    OMX_ERRORTYPE e;                                       \
    if( (e = omxcmnd(__VA_ARGS__)) != OMX_ErrorNone ) {    \
      printf("%s:%i Error executing %s (0x%08x)\n",        \
              __FILE__,__LINE__, \
             #omxcmnd,e);                                  \
      RenderRelease(rh);                                   \
      return NULL;                                         \
    }                                                      \
} while(0)
#define ABORT(rh,omxcmnd,...)      do {                    \
    OMX_ERRORTYPE e;                                       \
    if( (e = omxcmnd(__VA_ARGS__)) != OMX_ErrorNone ) {    \
      printf("%s:%i Error executing %s (0x%08x)\n",        \
              __FILE__,__LINE__, \
             #omxcmnd,e);  \
      return NULL;                                         \
    }                                                      \
} while(0)
#define WARN(rh,omxcmnd,...)      do {                             \
    OMX_ERRORTYPE e;                                               \
    if( (e = omxcmnd(__VA_ARGS__)) != OMX_ErrorNone ) {            \
      printf("%s:%i Warning! error executing %s (0x%08x)\n",       \
        __FILE__,__LINE__, \
        #omxcmnd,e); \
    }                                                              \
} while(0)
// Local structures
typedef struct _Buffer *Buffer;
struct _Buffer {
    OMX_BUFFERHEADERTYPE  *Header;
    Buffer Next;
    uint16_t InUse;
    __attribute__((__aligned__(16)))
    unsigned char Buffer[1];
};
struct _Renderer {
    char Name[32];
    OMX_HANDLETYPE Decode;
    OMX_U32   DecodePort;
    OMX_HANDLETYPE Render;
    OMX_U32   RenderPort;
    Buffer DecodeBuffer;
    Buffer LastUsed;
    uint32_t  NumberOfBuffers;
    int ReadyToRender;
    int Rendering;
    int IsInvisible;
};
typedef struct _Renderer *Renderer;
//
// Local data
//
static DISPMANX_DISPLAY_HANDLE_T Display;
static DISPMANX_MODEINFO_T DisplayInfo;
static Renderer Background;
static OMX_HANDLETYPE NullSink;
static OMX_U32 NullSinkPort;

// Used for logging and debugging
static char *StateToString(OMX_STATETYPE state) {
    switch(state) {
      case OMX_StateInvalid:          return "OMX_StateInvalid";
      case OMX_StateLoaded:           return "OMX_StateLoaded";
      case OMX_StateIdle:             return "OMX_StateIdle";
      case OMX_StateExecuting:        return "OMX_StateExecuting";
      case OMX_StatePause:            return "OMX_StatePause";
      case OMX_StateWaitForResources: return "OMX_StateWaitForResources";
      default: return "OMX_StateUnknown";
    }
    return "OMX_StateUnknown";
}

static OMX_ERRORTYPE CB_EventHandler(OMX_HANDLETYPE hComponent,
                                     OMX_PTR pAppData,
                                     OMX_EVENTTYPE eEvent,
                                     OMX_U32 nData1,
                                     OMX_U32 nData2,
                                     OMX_PTR pEventData) {
    Renderer r = (Renderer) pAppData;
    char *name = r ? r->Name : "Unknown";
    switch (eEvent) {
      case OMX_EventCmdComplete: break;
        switch (nData1) {
          case OMX_CommandStateSet:
            printf("%s state changed (%s) complete\n",name,StateToString(nData2));
            break;
          case OMX_CommandPortDisable:
            printf("%s port disable %d complete\n",name, nData2);
            break;
          case OMX_CommandPortEnable:
            printf("%s port enable %d complete\n",name, nData2);
            break;
          case OMX_CommandFlush:
            printf("%s port flush %d complete\n",name, nData2);
            break;
          case OMX_CommandMarkBuffer:
            printf("%s mark buffer %d complete\n",name, nData2);
            break;
          default:
            printf("%s something completed %d\n",name, nData2);
         }
        break;
      case OMX_EventError:
        switch (nData1) {
          case OMX_ErrorPortUnpopulated:
            printf("CB ignore error: port unpopulated (%d)\n", nData2);
            break;
          case OMX_ErrorSameState:
            printf("CB ignore error: same state %s (%s)\n",name,StateToString(nData2));
            break;
          case OMX_ErrorBadParameter:
            printf("CB ignore error: bad parameter (%d)\n", nData2);
            break;
          case OMX_ErrorIncorrectStateTransition:
            printf("CB %s incorrect state transition (%s) %8x %p\n",
                   name, StateToString(nData2),nData1,pEventData);
            break;
          case OMX_ErrorBadPortIndex:
            printf("CB bad port index (%d)\n", nData2);
            break;
          case OMX_ErrorStreamCorrupt:
            printf("CB stream corrupt (%d)\n", nData2);
            break;
          case OMX_ErrorInsufficientResources:
            printf("CB insufficient resources (%d)\n", nData2);
            break;
          case OMX_ErrorUnsupportedSetting:
            printf("CB unsupported setting (%d)\n", nData2);
            break;
          case OMX_ErrorOverflow:
            printf("CB overflow (%d)\n", nData2);
            break;
          case OMX_ErrorDiskFull:
            printf("CB disk full (%d)\n", nData2);
            break;
          case OMX_ErrorMaxFileSize:
            printf("CB max file size (%d)\n", nData2);
            break;
          case OMX_ErrorDrmUnauthorised:
            printf("CB drm file is unauthorised (%d)\n", nData2);
            break;
          case OMX_ErrorDrmExpired:
            printf("CB drm file has expired (%d)\n", nData2);
            break;
          case OMX_ErrorDrmGeneral:
            printf("CB drm library error (%d)\n", nData2);
            break;
          default:
            printf("CB unexpected error (%d)\n", nData2);
            break;
          }
        break;
      case OMX_EventBufferFlag:
//        printf("CB buffer flag %d/%x\n", nData1, nData2);
        break;
      case OMX_EventPortSettingsChanged:
        printf("CB port settings changed for %s Port %d\n", name,nData1);
        if( r && nData1 == (r->DecodePort+1) ) {
          r->ReadyToRender = 1;
        }
        break;
      case OMX_EventMark:
        printf("CB buffer mark %p\n", pEventData);
        break;
      case OMX_EventParamOrConfigChanged:
        printf("CB param/config 0x%X on port %d changed\n", nData2, nData1);
        break;
      default:
        printf("CB unknown event 0x%08x\n", eEvent);
        break;
    }
    return 0;
}
static OMX_ERRORTYPE CB_EmptyBufferDone(OMX_HANDLETYPE hComponent,
                                        OMX_PTR pAppData,
                                        OMX_BUFFERHEADERTYPE* pBuffer) {

    Buffer buff;
    buff = (void *) pBuffer->pBuffer - offsetof(struct _Buffer,Buffer);
    buff->InUse = 0;
//    printf("CB_EmptyBufferDone %p %p\n",buff,pBuffer);
    return 0;
}
 
static OMX_ERRORTYPE CB_FillBufferDone(OMX_HANDLETYPE hComponent,
                                       OMX_PTR pAppData,
                                       OMX_BUFFERHEADERTYPE* pBuffer) {
    printf("CB_FillBufferDone\n");
    return 0;
}
static int WaitForPortState(Renderer r,OMX_HANDLETYPE h,OMX_U32 port,OMX_BOOL state,int wait) {
    time_t then = time(NULL) + wait;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;

    while(time(NULL) < then) {
      memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
      portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
      portdef.nVersion.nVersion = OMX_VERSION;
      portdef.nPortIndex = port;
      WARN(r,OMX_GetParameter,h, OMX_IndexParamPortDefinition, &portdef);
      if(state && portdef.bEnabled)
        return 1;
      if(!state && !portdef.bEnabled)
        return 1;
      usleep(100000);
    }
    printf("Port never changed state!!\n");
    return 0;
}
static int WaitForComponentState(Renderer r,OMX_HANDLETYPE h,OMX_STATETYPE state,int wait) {
    time_t then = time(NULL) + wait;
    OMX_STATETYPE currentstate;
    while(time(NULL) < then) {
      WARN(r,OMX_GetState,h, &currentstate);
      if( currentstate == state )
        return 1;
      usleep(100000);
    }
    return 0;
}
static int GetBasePort(Renderer r,OMX_HANDLETYPE h) {
    OMX_PORT_PARAM_TYPE port;
    port.nSize = sizeof(OMX_PORT_PARAM_TYPE);
    port.nVersion.nVersion = OMX_VERSION;
    port.nStartPortNumber = 0;
    WARN(r,OMX_GetParameter,h, OMX_IndexParamVideoInit, &port);
    if( port.nStartPortNumber )
      return port.nStartPortNumber;
    WARN(r,OMX_GetParameter,h, OMX_IndexParamImageInit, &port);
    if( port.nStartPortNumber )
      return port.nStartPortNumber;
    WARN(r,OMX_GetParameter,h, OMX_IndexParamAudioInit, &port);
    if( port.nStartPortNumber )
      return port.nStartPortNumber;
    WARN(r,OMX_GetParameter,h, OMX_IndexParamOtherInit, &port);
    
    return port.nStartPortNumber;
}
//
// Setup the render and decoder and allocate buffers
//
static Renderer SetupRenderer(char *Name,char *Decode,char *Render,int Compression) {
    Renderer rend;
    OMX_CALLBACKTYPE callbacks;
    OMX_STATETYPE state;

    rend = calloc(1,sizeof(struct _Renderer));

    strncpy(rend->Name,Name,32);
    rend->Name[31] = 0;
    // Create the render and decoder objects
    callbacks.EventHandler    = CB_EventHandler;
    callbacks.EmptyBufferDone = CB_EmptyBufferDone;
    callbacks.FillBufferDone  = CB_FillBufferDone;

    FATAL(rend,OMX_GetHandle,&rend->Decode,Decode,rend,&callbacks);
    FATAL(rend,OMX_GetHandle,&rend->Render,Render,rend,&callbacks);
    OMX_U32 decodeport = rend->DecodePort = GetBasePort(rend,rend->Decode);
    OMX_U32 renderport = rend->RenderPort = GetBasePort(rend,rend->Render);
    // Disable all the ports
    WARN(rend,OMX_SendCommand,rend->Decode,   OMX_CommandPortDisable, decodeport,   NULL);
    WARN(rend,OMX_SendCommand,rend->Decode,   OMX_CommandPortDisable, decodeport+1, NULL);
    WARN(rend,OMX_SendCommand,rend->Render,   OMX_CommandPortDisable, renderport,   NULL);
    // Set the input format
    // to do this need to know port type
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    portdef.nVersion.nVersion = OMX_VERSION;
    portdef.nPortIndex = decodeport;
    FATAL(rend,OMX_GetParameter,rend->Decode, OMX_IndexParamPortDefinition, &portdef);
    switch(portdef.eDomain) {
      case OMX_PortDomainAudio: {
        OMX_AUDIO_PARAM_PORTFORMATTYPE format;
        memset(&format, 0, sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE));
        format.nSize = sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE);
        format.nVersion.nVersion = OMX_VERSION;
        format.nPortIndex = decodeport;
        format.eEncoding  = Compression;
        FATAL(rend,OMX_SetParameter,rend->Decode, OMX_IndexParamAudioPortFormat, &format);
      } break;
      case OMX_PortDomainVideo: {
        OMX_VIDEO_PARAM_PORTFORMATTYPE format;
        memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
        format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
        format.nVersion.nVersion = OMX_VERSION;
        format.nPortIndex = decodeport;
        format.eCompressionFormat = Compression;
        FATAL(rend,OMX_SetParameter,rend->Decode, OMX_IndexParamVideoPortFormat, &format);
      } break;
      case OMX_PortDomainImage: {
        OMX_IMAGE_PARAM_PORTFORMATTYPE format;
        memset(&format, 0, sizeof(OMX_IMAGE_PARAM_PORTFORMATTYPE));
        format.nSize = sizeof(OMX_IMAGE_PARAM_PORTFORMATTYPE);
        format.nVersion.nVersion = OMX_VERSION;
        format.nPortIndex = decodeport;
        format.eCompressionFormat = Compression;
        FATAL(rend,OMX_SetParameter,rend->Decode, OMX_IndexParamImagePortFormat, &format);
      } break;
      case OMX_PortDomainOther: {
        OMX_OTHER_PARAM_PORTFORMATTYPE format;
        memset(&format, 0, sizeof(OMX_OTHER_PARAM_PORTFORMATTYPE));
        format.nSize = sizeof(OMX_OTHER_PARAM_PORTFORMATTYPE);
        format.nVersion.nVersion = OMX_VERSION;
        format.nPortIndex = decodeport;
        format.eFormat = Compression;
        FATAL(rend,OMX_SetParameter,rend->Decode, OMX_IndexParamOtherPortFormat, &format);
      } break;
      default: 
        printf("Unknown port format for %s %i\n",rend->Name,portdef.eDomain);
        break;
    }
    // Need to assign buffers to the decoder
    // The decoder needs to be in idle state and the port enabled
    WARN(rend,OMX_SendCommand,rend->Decode,OMX_CommandStateSet, OMX_StateIdle, NULL);
    WARN(rend,OMX_SendCommand,rend->Decode,OMX_CommandPortEnable, decodeport, NULL);
    WaitForPortState(rend,rend->Decode,decodeport,OMX_TRUE,10);
    WaitForComponentState(rend,rend->Decode,OMX_StateIdle,10);
    
    // Just in case the the state change or port change
    // is still pending, need to check they have happened
    // Also need the port info for the buffers
    memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    portdef.nVersion.nVersion = OMX_VERSION;
    portdef.nPortIndex = decodeport;
    FATAL(rend,OMX_GetParameter,rend->Decode, OMX_IndexParamPortDefinition, &portdef);
    FATAL(rend,OMX_GetState,rend->Decode, &state);
    if( state != OMX_StateIdle || portdef.bEnabled == OMX_FALSE ) {
      printf("Error: unable to get decoder into required state\n");
      RenderRelease(rend);
      return NULL;
    }
    // Create and add the buffers
    // Calculate the size
    int bufsize = portdef.nBufferSize + offsetof(struct _Buffer, Buffer);
    for(int i=0; i < portdef.nBufferCountActual; i++) {
      Buffer buf;
      int e;
      OMX_BUFFERHEADERTYPE *bfh;

      e = posix_memalign((void **)&buf,portdef.nBufferAlignment,bufsize);
      if(e) {
        printf("Unable to allocate memory for buffers: %s\n",strerror(e));
        RenderRelease(rend);
        return NULL;
      }
      FATAL(rend,OMX_UseBuffer,rend->Decode,&bfh,decodeport, NULL, portdef.nBufferSize, buf->Buffer);
      buf->Header = bfh;
      bfh->pAppPrivate = buf;
      buf->Next = rend->DecodeBuffer;
      rend->DecodeBuffer = buf;
    }
    rend->NumberOfBuffers = portdef.nBufferCountActual;
    // Start the decoder executing
    FATAL(rend,OMX_SendCommand,rend->Decode,OMX_CommandStateSet, OMX_StateExecuting, NULL);
    return rend;
}
//
// Must be called first
// Initialises everything and sets up a NullSink
// 
int RenderInitialise() {
    OMX_ERRORTYPE e;

    bcm_host_init();
    if( (Display = vc_dispmanx_display_open( 0 )) == 0 ) {
      printf("Error opening display\n");
      bcm_host_deinit();
      return -1;
    }
    if( vc_dispmanx_display_get_info( Display, &DisplayInfo) < 0 ) {
      printf("Failed to get display information\n");
      vc_dispmanx_display_close(Display);
      bcm_host_deinit();
      return -1;
    }
    if((e=OMX_Init()) != OMX_ErrorNone) {
      printf("Error initialising OMX: 0x%08x\n",e);
      vc_dispmanx_display_close(Display);
      bcm_host_deinit();
      return -1;
    }
    OMX_CALLBACKTYPE callbacks;
    // Create the render and decoder objects
    callbacks.EventHandler    = CB_EventHandler;
    callbacks.EmptyBufferDone = CB_EmptyBufferDone;
    callbacks.FillBufferDone  = CB_FillBufferDone;

    OMX_GetHandle(&NullSink,NULL_SINK,NULL,&callbacks);
    NullSinkPort = GetBasePort(NULL,NullSink);
    return 0;
}
void RenderDeInitialise() {
    if(NullSink) {
      OMX_SendCommand(NullSink,OMX_CommandPortDisable,NullSinkPort,NULL);
      WARN(NULL,OMX_SendCommand,NullSink,OMX_CommandStateSet, OMX_StateIdle, NULL);
      WaitForComponentState(NULL,NullSink,OMX_StateIdle,2);
      WARN(NULL,OMX_SendCommand,NullSink,OMX_CommandStateSet, OMX_StateLoaded, NULL);
      WaitForComponentState(NULL,NullSink,OMX_StateLoaded,2);
      OMX_FreeHandle(NullSink);
      NullSink = NULL;
    }
    if(Background) {
      RenderRelease(Background);
      Background = NULL;
    }
    OMX_Deinit();
    vc_dispmanx_display_close(Display);
    bcm_host_deinit();
    return;
}
//
// Supposed to free everything
//
void RenderRelease(void *handle) {
    Renderer r = handle;
    Buffer buff,b;
    if(r == NULL)
      return;

    // Disable and flush decoder port
    if(r->Decode) {
      OMX_SendCommand(r->Decode,OMX_CommandPortDisable,r->DecodePort+1,NULL);
      OMX_SendCommand(r->Decode,OMX_CommandFlush,r->DecodePort+1, NULL);
      OMX_SendCommand(r->Decode,OMX_CommandPortDisable,r->DecodePort,NULL);
    }
    // Disable render ports
    if(r->Render) {
      OMX_SendCommand(r->Render,OMX_CommandPortDisable,r->RenderPort,NULL);
      OMX_SendCommand(r->Render,OMX_CommandPortDisable,r->RenderPort+1,NULL);
    }
    // Free decode buffers
    if(r->Decode) {
      for(buff = r->DecodeBuffer; buff;) {
        b = buff;
        buff = buff->Next;
        OMX_FreeBuffer(r->Decode,r->DecodePort,b->Header);
        free(b);
      }
    }
    // Put into state OMX_StateLoaded and release
    if(r->Decode) {
      WARN(r,OMX_SendCommand,r->Decode,OMX_CommandStateSet, OMX_StateIdle, NULL);
      WaitForComponentState(r,r->Decode,OMX_StateIdle,2);
      WARN(r,OMX_SendCommand,r->Decode,OMX_CommandStateSet, OMX_StateLoaded, NULL);
      WaitForComponentState(r,r->Decode,OMX_StateLoaded,2);
      OMX_FreeHandle(r->Decode);
    }
    if(r->Render) {
      WARN(r,OMX_SendCommand,r->Render,OMX_CommandStateSet, OMX_StateIdle, NULL);
      WaitForComponentState(r,r->Render,OMX_StateIdle,2);
      WARN(r,OMX_SendCommand,r->Render,OMX_CommandStateSet, OMX_StateLoaded, NULL);
      WaitForComponentState(r,r->Render,OMX_StateLoaded,2);
      OMX_FreeHandle(r->Render);
    }
    free(r);
}
//
// Sets up a decoder/renderer for a H264 stream
//
void *RenderNew(char *Name) {
    return SetupRenderer(Name,VIDEO_DECODE,VIDEO_RENDER,OMX_VIDEO_CodingAVC);
}
//
// Get a buffer to put stream data in
// Return a pointer to the buffer and sets
// length to how long it is
// If no buffers are available returns NULL
// and leaves length alone
//
void *RenderGetBuffer(void *handle,int32_t *length) {
    Renderer r = handle;
    Buffer buff;
    int i;

    if(r == NULL) {
      printf("NULL HANDLE\n");
      return NULL;
    }
//    buff = r->LastUsed && r->LastUsed->Next ? r->LastUsed->Next : r->DecodeBuffer;
    buff = r->DecodeBuffer;
    for(i=0; i < r->NumberOfBuffers; i++) {
      if(buff->InUse == 0 && buff->Header->nFilledLen == 0)
        break;
      buff = buff->Next ? buff->Next : r->DecodeBuffer;
    }
    if(buff == NULL || buff->InUse || buff->Header->nFilledLen)
      return NULL;
    if(length)
      *length = buff->Header->nAllocLen;
    return buff->Header->pBuffer;
}
//
// Process the data in buffer.
// "data" pointer must have been obtained by calling RenderGetBuffer
// length is how much data is in buffer and a non zero
// flag indicates the End-Of-Stream
//
void *RenderProcessBuffer(void *handle,void *data,int32_t length,int flag) {
    Renderer r = handle;
    Buffer buff;
    if(r == NULL) return NULL;
    buff = data - offsetof(struct _Buffer,Buffer);
    buff->Header->nFilledLen = length;
    buff->InUse++;
    if(flag)
      buff->Header->nFlags = OMX_BUFFERFLAG_EOS;
    ABORT(r,OMX_EmptyThisBuffer,r->Decode, buff->Header);
    if(r->ReadyToRender == 0) {
      return r;
    }
    if(r->Rendering)
      return r;
    if(r->IsInvisible)
      return r;

    // The tunnel to the renderer has to be set up
    ABORT(r,OMX_SetupTunnel,r->Decode,r->DecodePort+1,r->Render,r->RenderPort);
    ABORT(r,OMX_SendCommand,r->Decode,OMX_CommandPortEnable,r->DecodePort+1, NULL);
    ABORT(r,OMX_SendCommand,r->Render,OMX_CommandPortEnable,r->RenderPort, NULL);
    ABORT(r,OMX_SendCommand,r->Render,OMX_CommandStateSet,OMX_StateIdle, NULL);
//    WARN(r,OMX_SendCommand,sink,OMX_CommandStateSet,OMX_StateExecuting, NULL);
    OMX_SendCommand(r->Render,OMX_CommandStateSet,OMX_StateExecuting, NULL);
    r->Rendering = 1;
    return r;
}
//
// Sets the background colour to colour. The colour should be 24bit RGB.
//
void *RenderSetBackground(void *handle, uint32_t colour) {
    Renderer r;
    OMX_PARAM_SOURCETYPE source;

    if(Background == NULL) {
      OMX_CALLBACKTYPE callbacks;
      r = Background = calloc(1,sizeof(struct _Renderer));

      strcpy(r->Name,"Background");
      // Create the render and decoder objects
      callbacks.EventHandler    = CB_EventHandler;
      callbacks.EmptyBufferDone = CB_EmptyBufferDone;
      callbacks.FillBufferDone  = CB_FillBufferDone;

      FATAL(r,OMX_GetHandle,&r->Decode,BGRND_SOURCE,r,&callbacks);
      FATAL(r,OMX_GetHandle,&r->Render,VIDEO_RENDER,r,&callbacks);
      r->DecodePort = GetBasePort(r,r->Decode);
      r->RenderPort = GetBasePort(r,r->Render);
       // Disable all the ports
      WARN(r,OMX_SendCommand,r->Decode,   OMX_CommandPortDisable, r->DecodePort,   NULL);
      WARN(r,OMX_SendCommand,r->Render,   OMX_CommandPortDisable, r->RenderPort,   NULL);
      // Start the decoder executing
      FATAL(r,OMX_SendCommand,r->Decode,OMX_CommandStateSet, OMX_StateIdle, NULL);
      FATAL(r,OMX_SendCommand,r->Decode,OMX_CommandStateSet, OMX_StateExecuting, NULL);
      // Set to fullscreen
      RendererSetFullScreen(r,0,0,1.0);
      // Set the input format and colour
      memset(&source, 0, sizeof(OMX_PARAM_SOURCETYPE));
      source.nSize = sizeof(OMX_PARAM_SOURCETYPE);
      source.nVersion.nVersion = OMX_VERSION;
      source.nPortIndex = r->DecodePort;
      source.eType = OMX_SOURCE_COLOUR;
      source.nParam = RGB888_TO_RGB565(colour);
      FATAL(r,OMX_SetParameter,r->Decode, OMX_IndexParamSource, &source);
      // Set up the tunnel
      ABORT(r,OMX_SetupTunnel,r->Decode,r->DecodePort,r->Render,r->RenderPort);
      ABORT(r,OMX_SendCommand,r->Decode,OMX_CommandPortEnable,r->DecodePort, NULL);
      ABORT(r,OMX_SendCommand,r->Render,OMX_CommandPortEnable,r->RenderPort, NULL);
      ABORT(r,OMX_SendCommand,r->Render,OMX_CommandStateSet,OMX_StateIdle, NULL);
      OMX_SendCommand(r->Render,OMX_CommandStateSet,OMX_StateExecuting, NULL);
      return r;
    }
    r = Background;
    WARN(r,OMX_SendCommand,r->Decode,OMX_CommandPortDisable,r->DecodePort,NULL);

    memset(&source, 0, sizeof(OMX_PARAM_SOURCETYPE));
    source.nSize = sizeof(OMX_PARAM_SOURCETYPE);
    source.nVersion.nVersion = OMX_VERSION;
    source.nPortIndex = r->DecodePort;
    WARN(r,OMX_GetParameter,r->Decode, OMX_IndexParamSource, &source);
    source.nParam = RGB888_TO_RGB565(colour);
    WARN(r,OMX_SetParameter,r->Decode, OMX_IndexParamSource, &source);
    WARN(r,OMX_SendCommand,r->Decode,OMX_CommandPortEnable,r->DecodePort,NULL);
    return r;    
}
// Make the stream invisible
void RendererSetInvisible(void *handle) {
    Renderer r  = handle;

    if(r == NULL)
      return;
    if(r->IsInvisible)
      return;
    r->IsInvisible = 1;
    if(r->Rendering == 0)
      return;

    // Disable the port
    WARN(r,OMX_SendCommand,r->Decode,OMX_CommandPortDisable,r->DecodePort+1,NULL);
    WARN(r,OMX_SendCommand,r->Render,OMX_CommandPortDisable,r->RenderPort,NULL);
    // Release the buffers
    WARN(r,OMX_SendCommand,r->Decode,OMX_CommandFlush,r->DecodePort+1, NULL);
    WARN(r,OMX_SendCommand,r->Render,OMX_CommandFlush,r->RenderPort, NULL);
    // Add tunnel to NullSink
    WARN(r,OMX_SetupTunnel,r->Decode,r->DecodePort+1,NullSink,NullSinkPort);
    WARN(r,OMX_SendCommand,r->Decode,OMX_CommandPortEnable,r->DecodePort+1, NULL);
    return;
}
// Make the stream visible
void RendererSetVisible(void *handle) {
    Renderer r  = handle;

    if(r == NULL)
      return;
    if(r->IsInvisible == 0)
      return;
    r->IsInvisible = 0;
    if(r->Rendering == 0)
      return;
    // Disable the port
    WARN(r,OMX_SendCommand,r->Decode,OMX_CommandPortDisable,r->DecodePort+1,NULL);
    // Release the buffers
    WARN(r,OMX_SendCommand,r->Decode,OMX_CommandFlush,r->DecodePort+1, NULL);
    // Add tunnel
    WARN(r,OMX_SetupTunnel,r->Decode,r->DecodePort+1,r->Render,r->RenderPort);
    WARN(r,OMX_SendCommand,r->Decode,OMX_CommandPortEnable,r->DecodePort+1, NULL);
    WARN(r,OMX_SendCommand,r->Render,OMX_CommandPortEnable,r->RenderPort, NULL);
    WARN(r,OMX_SendCommand,r->Render,OMX_CommandStateSet,OMX_StateIdle, NULL);
    WARN(r,OMX_SendCommand,r->Render,OMX_CommandStateSet,OMX_StateExecuting, NULL);
    return;
}
// Display stream  fullscreen.
void RendererSetFullScreen(void *handle,int aspect,int layer,double alpha) {
    Renderer r  = handle;
    int alphavalue;
    OMX_CONFIG_DISPLAYREGIONTYPE dr;
    OMX_ERRORTYPE e;
    if(r == NULL)
      return;

    if(r->IsInvisible) {
      RendererSetVisible(handle);
    }
    memset(&dr, 0, sizeof(dr));
    dr.nSize = sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
    dr.nVersion.nVersion = OMX_VERSION;
    dr.nPortIndex = 90;

    alphavalue = 255 * alpha;
    // Correct for any mistakes
    dr.alpha = alphavalue > 255 ? 255 : alphavalue < 0 ? 0 : alphavalue;
    dr.noaspect = aspect ? OMX_FALSE : OMX_TRUE;
    dr.layer = layer;
    dr.fullscreen = OMX_TRUE;
    dr.set = OMX_DISPLAY_SET_ALPHA | OMX_DISPLAY_SET_NOASPECT | OMX_DISPLAY_SET_FULLSCREEN;
    if((e = OMX_SetParameter(r->Render,OMX_IndexConfigDisplayRegion,&dr)) != OMX_ErrorNone) {
      printf("Error setting fullscreen\n");
    }
}
// Display stream in rectangle.
void RendererSetRectangle(void *handle,double X,double Y,double W,double H,int aspect,int layer,double alpha) {
    Renderer r  = handle;
    int alphavalue;
    OMX_CONFIG_DISPLAYREGIONTYPE dr;
    OMX_ERRORTYPE e;
    if(r == NULL)
      return;

    memset(&dr, 0, sizeof(dr));
    dr.nSize = sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
    dr.nVersion.nVersion = OMX_VERSION;
    dr.nPortIndex = 90;

    alphavalue = 255 * alpha;
    
    dr.fullscreen = OMX_FALSE;
    dr.noaspect = aspect ? OMX_FALSE : OMX_TRUE;
    dr.dest_rect.x_offset = (0.5 + DisplayInfo.width  * X);
    dr.dest_rect.y_offset = (0.5 + DisplayInfo.height * Y);
    dr.dest_rect.width    = (0.5 + DisplayInfo.width  * W);
    dr.dest_rect.height   = (0.5 + DisplayInfo.height * H);
    dr.alpha = alphavalue > 255 ? 255 : alphavalue < 0 ? 0 : alphavalue;
    dr.layer = layer;
    dr.set = OMX_DISPLAY_SET_NOASPECT   |
             OMX_DISPLAY_SET_ALPHA      |
             OMX_DISPLAY_SET_FULLSCREEN |
             OMX_DISPLAY_SET_LAYER      |
             OMX_DISPLAY_SET_DEST_RECT;
    if((e = OMX_SetParameter(r->Render,OMX_IndexConfigDisplayRegion,&dr)) != OMX_ErrorNone) {
      printf("Error setting fullscreen\n");
    }
    if(r->IsInvisible) {
      RendererSetVisible(handle);
    }
}
