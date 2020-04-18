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
#ifndef _MONITOR_H_INCLUDED_
#define _MONITOR_H_INCLUDED_

#define MAX_NAME_LENGTH   16

typedef struct _MonitorHandle *MonitorHandle;
struct _MonitorHandle {
    char   Name[MAX_NAME_LENGTH];               // Used for logging
    int    FileDescriptor;                      // File Descriptor to monitor
    void  *ReadCBData;                          // Data to pass to ReadCB
    void (*ReadCB)(MonitorHandle, void *);        // Called when there is data to read
    time_t NextHouseKeep;                       // When to call HouseKeepCB
    void  *HouseKeepData;                       // Data to pass to Housekeeping
    void (*HouseKeepCB)(MonitorHandle, void *);   // Called periodically
    void *DisEngageData;                        // Data to pass to DisEngageCB
    void (*DisEngageCB)(MonitorHandle, void *);   // Called when the handle is being destroyed
};
typedef struct _CurlComplete  *CurlComplete;
struct _CurlComplete {
    void (*Callback)(CURL *,CurlComplete);
    void *Data;
};
MonitorHandle MonitorNew(const char *);
int MonitorProcess(int);
#define MonitorSetReadFD(h,f)           (h)->FileDescriptor = (f)
#define MonitorSetReadData(h,d)         (h)->ReadCBData = (d)
#define MonitorSetReadCB(h,cb)          (h)->ReadCB = (cb)
#define MonitorSetHouseKeepingTime(h,t) (h)->NextHouseKeep = (t)
#define MonitorSetHouseKeepingCB(h,cb)  (h)->HouseKeepCB = (cb)
#define MonitorSetHouseKeepingData(h,d) (h)->HouseKeepData = (d)
#define MonitorGetName(h)               (h)->Name
#define MonitorGetReadFD(h)             (h)->FileDescriptor
#define MonitorClearReadFD(h)           MonitorSetReadFD((h),-1)
#endif
