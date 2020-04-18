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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <ctype.h>
#include <lirc_client.h>
#include <curl/curl.h>
#include "monitor.h"

#include "cctvplexer.h"
#include "render.h"
#include "monitor.h"

//
// In order to get at RTP stream and then the raw H264 data
// there are several steps that need to be taken.
//   1. Send a DESCRIBE
//      This gets a description of the stream. The only
//      attribute currently needed is the control URI
//   2. Send a SETUP
//      This is sent using the control extracted from 1.
//   3. Send a PLAY
//      Tells the stream to start playing
//


#define MINBUFSIZ     4096
#define TRANSPORT     "RTP/AVP/TCP;unicast;interleaved=0-1"

enum PktType {
    PT_RES_00 = 0,
    PT_NAL_01,
    PT_NAL_02,
    PT_NAL_03,
    PT_NAL_04,
    PT_NAL_05,
    PT_NAL_06,
    PT_NAL_07,
    PT_NAL_08,
    PT_NAL_09,
    PT_NAL_10,
    PT_NAL_11,
    PT_NAL_12,
    PT_NAL_13,
    PT_NAL_14,
    PT_NAL_15,
    PT_NAL_16,
    PT_NAL_17,
    PT_NAL_18,
    PT_NAL_19,
    PT_NAL_20,
    PT_NAL_21,
    PT_NAL_22,
    PT_NAL_23,
    PT_STAP_A,
    PT_STAP_B,
    PT_MTAP16,
    PT_MTAP24,
    PT_FU_A,
    PT_FU_B,
    PT_RES_30,
    PT_RES_31,
};
extern CURLM *CurlHandle;
#define my_curl_easy_setopt(A, B, C)                                \
  do {                                                              \
    int res;                                                        \
    res = curl_easy_setopt((A), (B), (C));                          \
    if(res != CURLE_OK)                                             \
      fprintf(stderr, "curl_easy_setopt(%s, %s, %s) failed: %d\n",  \
              #A, #B, #C, res);                                     \
  } while(0)

// The interleaved stream
static size_t RTPStream(char *ptr,size_t size, size_t nitems, void *userdata) {
    Camera cam = userdata;
    int inlength = size * nitems;
    int length = ((unsigned char) *(ptr+2)) * 256 + ((unsigned char) *(ptr+3));
    int buflen;
    // Check the length
    if((length+4) != inlength)
      exit(0);
    // Check the channel (exiting so I know)
    if(*(ptr+1))
      exit(0);
    // 4 bytes RTSP + 12 bytes RTP header
    char *payload = ptr + 4 + 12;
    int paylen = inlength - 4 - 12;
    // Remove any padding
    if( ptr[4] & 0x20 )
      paylen -= ptr[inlength-1];

    char *buffer = RenderGetBuffer(cam->RenderHandle,&buflen);
    if(buffer == NULL) {
      printf("Error getting buffer for camera %s\n",cam->Name);
      return inlength;
    }
    // handle packet type
    buflen = 0;
    enum PktType ptype = *payload & 0x1f;
    switch(ptype) {
      case PT_NAL_01:
      case PT_NAL_02:
      case PT_NAL_03:
      case PT_NAL_04:
      case PT_NAL_05:
      case PT_NAL_06:
      case PT_NAL_07:
      case PT_NAL_08:
      case PT_NAL_09:
      case PT_NAL_10:
      case PT_NAL_11:
      case PT_NAL_12:
      case PT_NAL_13:
      case PT_NAL_14:
      case PT_NAL_15:
      case PT_NAL_16:
      case PT_NAL_17:
      case PT_NAL_18:
      case PT_NAL_19:
      case PT_NAL_20:
      case PT_NAL_21:
      case PT_NAL_22:
      case PT_NAL_23:
        // Set the frame marker
        buffer[0] = buffer[1] = buffer[2] = 0; buffer[3] = 1;
        // Copy the payload
        memcpy(buffer+4,payload,paylen);
        buflen = paylen+4;
        break;
      case PT_STAP_A:
      case PT_STAP_B:
        printf("Unhandled STAP packet %i\n",ptype);
        break;
      case PT_MTAP16:
      case PT_MTAP24:
        printf("Unhandled MTAP packet %i\n",ptype);
        break;
      case PT_FU_B:
      case PT_FU_A: {
        // Extract NRI flags and move on
        int nri_flags = *payload++ & 0xe0;
        paylen--;
        // Start of fragment or continuation?
        if( *payload & 0x80 ) {
          // The start
          // Set the frame marker
          buffer[0] = buffer[1] = buffer[2] = 0; buffer[3] = 1;
          // Set nri flags
          *payload = (*payload & 0x1f) | nri_flags;
          memcpy(buffer+4,payload,paylen);
          buflen = paylen+4;
        }
        else {
          // Skip frag header
          payload++;
          paylen--;
          memcpy(buffer,payload,paylen);
          buflen = paylen;
        }
      } break;
      case PT_RES_00:
      case PT_RES_30:
      case PT_RES_31:
        printf("Reserved packet type %i\n",ptype);
        break;
    }
    RenderProcessBuffer(cam->RenderHandle,buffer,buflen,0);
    return inlength;
}
// Resubmit the interleave
static void StreamInterleaveDone(CURL *easy,CurlComplete cp) {
    // curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);
    curl_multi_add_handle(CurlHandle,easy);
    return;
}
// The stream is playing so setup interleave
static void StreamPlayDone(CURL *easy,CurlComplete cp) {
    Camera c = cp->Data;

    cp->Callback = StreamInterleaveDone;

    my_curl_easy_setopt(easy, CURLOPT_PRIVATE, cp);
    my_curl_easy_setopt(easy, CURLOPT_URL, c->RTSP.URL);
    my_curl_easy_setopt(easy, CURLOPT_INTERLEAVEFUNCTION, RTPStream);
    my_curl_easy_setopt(easy, CURLOPT_INTERLEAVEDATA, c);
    my_curl_easy_setopt(easy, CURLOPT_RTSP_REQUEST,(long)CURL_RTSPREQ_RECEIVE);
    curl_multi_add_handle(CurlHandle,easy);
}
// The stream has been setup so tell it to play
static void StreamSetupDone(CURL *easy,CurlComplete cp) {
    Camera c = cp->Data;

    cp->Callback = StreamPlayDone;

//    return;
    my_curl_easy_setopt(easy, CURLOPT_PRIVATE, cp); 
    my_curl_easy_setopt(easy, CURLOPT_URL, c->RTSP.URL);
    my_curl_easy_setopt(easy, CURLOPT_RTSP_STREAM_URI, c->RTSP.Control);
    my_curl_easy_setopt(easy, CURLOPT_RANGE, "0.000-");
    my_curl_easy_setopt(easy, CURLOPT_RTSP_REQUEST, (long)CURL_RTSPREQ_PLAY);
    curl_multi_add_handle(CurlHandle,easy);
}
// Describe has finished so setup the stream 
static void DescribeDone(CURL *easy,CurlComplete cp) {
    Camera c = cp->Data;

    cp->Callback = StreamSetupDone;

    my_curl_easy_setopt(easy, CURLOPT_PRIVATE, cp); 
    my_curl_easy_setopt(easy, CURLOPT_URL, c->RTSP.URL);
    my_curl_easy_setopt(easy, CURLOPT_RTSP_STREAM_URI, c->RTSP.Control);
    my_curl_easy_setopt(easy, CURLOPT_RTSP_TRANSPORT, TRANSPORT);
    my_curl_easy_setopt(easy, CURLOPT_RTSP_REQUEST, (long)CURL_RTSPREQ_SETUP);
    curl_multi_add_handle(CurlHandle,easy);
}
// Parses the the SDP response from DESCRIBE
// and extracts the control uri
static size_t ParseSDP(char *ptr,size_t size, size_t nitems, void *userdata) {
    int inlength = size * nitems;
    Camera c = userdata;
    int maxcp = c->RTSP.ContentLength - c->RTSP.BufferUsed;
    if(maxcp < 0 || maxcp < inlength) {
      printf("Something is wrong: maxcp:%i cl:%i length:%i\n",
             maxcp,c->RTSP.ContentLength,inlength);
      return 0;   // force abort
    }
    maxcp = maxcp > inlength ? inlength : maxcp;
    memcpy(c->RTSP.Buffer+c->RTSP.BufferUsed,ptr,maxcp);
    c->RTSP.BufferUsed += maxcp;
    if( c->RTSP.BufferUsed < c->RTSP.ContentLength )
      return inlength;
    // Terminate SDP data
    c->RTSP.Buffer[inlength] = 0;
    char *cur,*next;
    char *media   = NULL;
    
    for(cur = ptr; cur; cur = next) {
      if((next = strstr(cur,"\r\n")) != NULL) {
        *next = 0;
        next += 2;
      }
      switch(*cur) {
                  // Session Description
        case 'v':     break;  // Version
        case 'o':     break;  // Originator and session id
        case 's':     break;  // Session Name
        // case 'i':  break;  // Title/Information
        case 'u':     break;  // Description URI
        case 'e':     break;  // Contact email address
        case 'p':     break;  // Contact phone
        // case 'c':  break;  // Connection information
        // case 'b':  break;  // Bandwidth
        case 'z':     break;  // Time Zone adjustments
        // case 'k':  break;  // Encryption key
        // case 'a':  break;  // Session attributes
                  // Time description
        case 't':     break;  // Time session is active
        case 'r':     break;  // Repeat times
                  // Media description
        case 'm':             // Name and transport
          if(strncmp(cur,"m=video ",8) == 0) {
            media = cur + 2;
          }
        case 'i':     break;  // Title/Information
        case 'c':     break;  // Connection information
        case 'b':     break;  // Bandwidth information
        case 'k':     break;  // Encryption key
        case 'a':             // Attributes
          if( media ) {
            if(strncmp(cur,"a=control:",10) == 0) {
              if( c->RTSP.Control )
                free(c->RTSP.Control);
              c->RTSP.Control = strdup(cur+10);
            }
          }
          break;
        case 0:
          break;
        default:
          printf("Unhandled character %c\n",*cur);
          break;
      }
    }
    // If the data was extracted then need to setup the stream
//    curl_multi_add_handle(CurlHandle,easy);

    return inlength;
}
// Extracts the content length from
// DESCRIBE header response
static size_t ParseDescribeHeader(char *ptr,size_t size, size_t nitems, void *userdata) {
    int inlength = size * nitems;
    Camera c = userdata;
    //
    // According to rfc7826 18.17 Content-Length is mandatory
    // on all responses that have content beyond the header.
    //
    if(strncasecmp(ptr,"Content-Length: ", 15 < inlength ? 15 : inlength) == 0) {
      // The line is terminated by CRLF so it is safe to null terminate
      ptr[inlength-1] = 0;
      int cl = c->RTSP.ContentLength = atoi(ptr+15);
      // Add 1 to cl for NULL termination
      cl++;
      // Allocate the buffer
      if( cl > c->RTSP.BufferLength ) {
        if(c->RTSP.Buffer) {
          printf("Increasing buffer to %i\n",cl);
          free(c->RTSP.Buffer);
        }
        c->RTSP.Buffer = malloc(cl < MINBUFSIZ ? MINBUFSIZ : cl);
      }
      c->RTSP.BufferUsed = 0;
    }
    return inlength;
}
// The sequence to get the H264 stream
// is started by sending a DESCRIBE
void RtspStartStream(Camera c) {
    CURL *easy = curl_easy_init( );
    CurlComplete cp;
    // Need to reset some parameters before starting...
    c->RTSP.BufferUsed = 0;
    c->RTSP.ContentLength = 0;
    // Setup and execute the DESCRIBE
    cp = malloc(sizeof(struct _CurlComplete));
    cp->Data = c;
    cp->Callback = DescribeDone;
    my_curl_easy_setopt(easy, CURLOPT_PRIVATE, cp); 
    my_curl_easy_setopt(easy, CURLOPT_VERBOSE, 0L);
    my_curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 1L);
    my_curl_easy_setopt(easy, CURLOPT_URL, c->RTSP.URL);
    my_curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, ParseDescribeHeader);
    my_curl_easy_setopt(easy, CURLOPT_HEADERDATA, c);
    my_curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION,ParseSDP);
    my_curl_easy_setopt(easy, CURLOPT_WRITEDATA,c);
    my_curl_easy_setopt(easy, CURLOPT_RTSP_STREAM_URI, c->RTSP.URL);
    my_curl_easy_setopt(easy, CURLOPT_RTSP_REQUEST, (long)CURL_RTSPREQ_DESCRIBE);
    curl_multi_add_handle(CurlHandle,easy);
}
