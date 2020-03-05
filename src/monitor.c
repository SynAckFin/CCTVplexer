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
#include "monitor.h"

#define MAX_MONITORS      64
#define FLG_USED          0x0001

typedef struct _MonList *MonList;
struct _MonList {
    struct _MonitorHandle Handle;
    uint32_t Flags;
};
struct _MonList Monitor[MAX_MONITORS];
uint32_t  MaxInUse = 0;

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
// Monitor the handles for input
// for at most Timeout seconds
// returns the number of Handles
// read from plus the number of
// housekeeping calls. Returns
// after the first read and/or
// housekeeping event
int MonitorProcess(int Timeout) {
    fd_set readfds;
    int  maxfd = 0;
    time_t nexthk;
    struct timeval tv;
    int eventcnt = 0;
    

    // If there is a timeout then set nexthk to it otherwise set it to zero
    nexthk = Timeout > 0 ? time(NULL) + Timeout : 0;
    // Set up the readfds and the time of the next houskeeping
    // Compacts the array by moving handles from the end into
    // empty spaces
    FD_ZERO(&readfds);
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
    // Set the timeval
    time_t now = time(NULL);
    tv.tv_sec = nexthk == 0 ? 0 : now < nexthk ? nexthk - now : 0;
    tv.tv_usec = 0;
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
    return eventcnt;
}





