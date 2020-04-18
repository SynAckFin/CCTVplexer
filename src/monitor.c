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
#include <time.h>
#include <assert.h>
#include <sys/select.h>
#include <curl/curl.h>
#include "monitor.h"

#define MAX_MONITORS      64
#define FLG_USED          0x0001

CURLM *CurlHandle = NULL;
typedef struct _MonList *MonList;
struct _MonList {
    struct _MonitorHandle Handle;
    uint32_t Flags;
};
struct _MonList Monitor[MAX_MONITORS];
struct curl_waitfd CurlExtraFD[MAX_MONITORS];

uint32_t  MaxInUse = 0;

void MonitorInitialise() {
    if(CurlHandle)
      return;
    curl_global_init(CURL_GLOBAL_ALL);
    CurlHandle = curl_multi_init();
}
// Add an input source
MonitorHandle MonitorNew(const char *Name) {
    MonList ml = NULL;
    if(MaxInUse < (MAX_MONITORS-1))
      ml = &Monitor[MaxInUse++];
    else for(int i=0; i < MaxInUse; i++) {
      if( (Monitor[i].Flags & FLG_USED) == 0 ) {
        ml = &Monitor[i];
        break;
      }
    }
    if(ml == NULL)
      return NULL;
    strncpy(ml->Handle.Name,Name,MAX_NAME_LENGTH);
    ml->Handle.Name[MAX_NAME_LENGTH-1] = 0;
    ml->Flags |= FLG_USED;
    return &ml->Handle;
}
// Remove an input source
void MonitorRelease(MonitorHandle Handle) {
    if(Handle == NULL)
      return;
    // Not a mistake The structure handle points
    // to is really a monitor structure
    memset(Handle,0,sizeof(struct _MonitorHandle));
}
// Monitor the handles for input for at most Timeout seconds
// returns the number of Handles read from plus the number of
// housekeeping calls. Returns after the first read and/or
// housekeeping event
int MonitorProcess(int Timeout) {
    fd_set readfds,writefds,errorfds;
    int  maxfd = 0;
    time_t nexthk = 0;
    int eventcnt = 0;
    long curltout;

    // Get curls timeout recomendation
    curl_multi_timeout(CurlHandle, &curltout);
    Timeout *= 1000;
    // Determine how long to wait for input
    curltout = curltout < 0       ? Timeout  :
               curltout < Timeout ? curltout : Timeout;

    // Get fds from curl that need to be monitored
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&errorfds);
    curl_multi_fdset(CurlHandle,&readfds,&writefds,&errorfds,&maxfd);
    // Set up the readfds and the time of the next houskeeping
    // Compacts the array by moving handles from the end into
    // empty spaces
    for(int i=0; i < MaxInUse;i++ ) {
      // Compact the array
      while((Monitor[i].Flags & FLG_USED) == 0 && i < MaxInUse) {
        // Make sure isn't the end object
        if(i != (MaxInUse - 1)) {
          memcpy(&Monitor[i],&Monitor[MaxInUse-1],sizeof(struct _MonList));
          memset(&Monitor[MaxInUse-1],0,sizeof(struct _MonList));
        }
        MaxInUse--;
      }
      // Compacting the array might have put i out of bounds!
      if(i >= MaxInUse)
        continue;         // Let the loop condition take care of it
      // Should never reach here with an unset FLG_USED
      assert(Monitor[i].Flags & FLG_USED);
      // Add filehandle to readfds if its there and there is a callback function
      if( Monitor[i].Handle.FileDescriptor >= 0 && Monitor[i].Handle.ReadCB != NULL) {
        FD_SET(Monitor[i].Handle.FileDescriptor,&readfds);
        if( Monitor[i].Handle.FileDescriptor > maxfd )
          maxfd = Monitor[i].Handle.FileDescriptor;
      }
      // Check for housekeeping
      if(Monitor[i].Handle.NextHouseKeep && Monitor[i].Handle.HouseKeepCB) {
        nexthk = nexthk == 0 ? Monitor[i].Handle.NextHouseKeep :
                 Monitor[i].Handle.NextHouseKeep > nexthk ? nexthk : 
                 Monitor[i].Handle.NextHouseKeep;
      }
    }
    // Calculate the timeout
    struct timeval tv;
    int64_t time_ms;
    if( nexthk ) {
      gettimeofday(&tv,NULL);
      // convert to milliseconds
      time_ms = (uint64_t) tv.tv_sec * 1000 + tv.tv_usec/1000;
      // Calculate how long till nexthk
      time_ms = (uint64_t) nexthk * 1000 - time_ms;
      // A zero value could result in waiting Timeout seconds
      if(time_ms <= 0)
        time_ms = 1;
    }
    else {
      time_ms = 0;
    }
    // What comes first? curl or nexthk
    time_ms =  time_ms && time_ms < curltout ? time_ms : curltout;
    // Make sure it isn't negative or zero
    // Set the timeout
    tv.tv_sec = time_ms/1000;
    tv.tv_usec = (time_ms%1000)*1000;
    // Finally,
    int sr;
    if( (sr = select(maxfd+1,&readfds,NULL,NULL,&tv)) < 0 ) {
      perror("select error");
      return 0;
    }
    else if(sr) {
      // There is no limit to what the callbacks can do
      // They might have added/removed/closed handles or
      // even changed callbacks so have be careful.
      for(int i=0,hcnt = MaxInUse; i < hcnt; i++) {
        // For convenience
        MonList       ml = &Monitor[i];
        MonitorHandle mh = &ml->Handle;
        // Might have been changed by a callback so test
        if( (ml->Flags & FLG_USED) == 0 || mh->FileDescriptor < 0 || mh->ReadCB == NULL)
          continue;
        
        if( FD_ISSET(mh->FileDescriptor,&readfds) ) {
          eventcnt++;
          mh->ReadCB(mh,mh->ReadCBData);
        }
      }
    }
    // Let curl perform anything it wants
    int running = 0;
    curl_multi_perform(CurlHandle,&running);
    if(0 && running)
      printf("%i still running\n",running);
    // Everything has been read so test for housekeeping events
    for(int i=0,hcnt = MaxInUse; i < hcnt; i++) {
      // For convenience
      MonList       ml = &Monitor[i];
      MonitorHandle mh = &ml->Handle;
      // Might have been changed by a callback so test
      if( (ml->Flags & FLG_USED) == 0 || mh->HouseKeepCB == NULL)
        continue;

      if( mh->NextHouseKeep && mh->NextHouseKeep <= time(NULL) ) {
        mh->NextHouseKeep = 0;
        eventcnt++;
        mh->HouseKeepCB(mh,mh->HouseKeepData);
      }
    }
    // Process curl messages
    struct CURLMsg *m;
    do {
      int msgq = 0;
      m = curl_multi_info_read(CurlHandle, &msgq);
      if(m) {
        switch(m->msg) {
          case CURLMSG_DONE: {
            CurlComplete cp = NULL;
            curl_easy_getinfo(m->easy_handle,CURLINFO_PRIVATE,&cp);
            if(m->data.whatever)
              printf("MESSAGE: %s\n",(char *) m->data.whatever);
            CURL *e = m->easy_handle;
            curl_multi_remove_handle(CurlHandle,e);
            if(cp)
              cp->Callback(e,cp);
            else
              curl_easy_cleanup(e);
          } break;
          default:
//            printf("MSG: %i\n",m->msg);
            break;
        }
      }
    } while(m);
    return eventcnt;
}





