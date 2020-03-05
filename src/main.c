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

#include "cctvplexer.h"
#include "render.h"
#include "monitor.h"

static int Stop = 0;
static void sighandler(int iSignal) {
  printf("signal caught: %d - exiting\n", iSignal);
  if(Stop >= 3) exit(0);
  Stop++;
}
static pid_t RunStream(Camera cam) {
    if(pipe(cam->StreamPipe) < 0) {
      printf("Error: Couldn't create pipe: %s\n",strerror(errno));
      return -1;
    }
    if( (cam->Child = fork()) != 0 ) {
      // Main process
      if( cam->Child < 0 ) {
        printf("Error: Couldn't fork process: %s\n",strerror(errno));
        return -1;
      }
      // Close the write end of the pipe
      close(cam->StreamPipe[1]);
      return cam->Child;
    }
    // This is the child process
    // Close the read end
    close(cam->StreamPipe[0]);
    // close stdin
    close(0);
    // Set the stdout to write end of pipe
    dup2(cam->StreamPipe[1],1);
    // Set stdin as /dev/null
    int infd = open("/dev/null",O_RDONLY);
    // Close all other file descriptors (leave stderr)
    for(int i = 3; i < 1000; i++)
      close(i);
    if(infd != 0)
      dup2(infd,0);
    execv(cam->StreamCommand[0],cam->StreamCommand);
    fprintf(stderr,"Failed to execute %s: %s\n",cam->StreamCommand[0],strerror(errno));
    exit(0);
    return 0;
}
static void SetView(Plexer p,int32_t view) {
    if(p == NULL || view < 0 || view >= p->ViewCount)
      return;

    p->CurrentView = view;
    RenderSetBackground(NULL,p->View[view].Background);
    // There is a glitch on some TVs when a
    // fullscreen render is placed underneath
    // other renders. To avoid this do the
    // "invisibles" and then the others
    Camera c = p->Camera;
    CameraView v = p->View[view].View;
    for(int i=0; i < p->CameraCount; i++, v++, c++) {
      if( v->Visible == 0 ) {
        RendererSetInvisible(c->RenderHandle);
      }
    }
    c = p->Camera;
    v = p->View[view].View;
    for(int i=0; i < p->CameraCount; i++, v++, c++) {
      if( v->Visible == 0 ) {
        continue;
      }
      else if( v->FullScreen ) {
        RendererSetFullScreen(c->RenderHandle,v->KeepAspect,v->Layer,v->Alpha);
      }
      else {
        RendererSetRectangle(c->RenderHandle,v->X,v->Y,v->W,v->H,v->KeepAspect,v->Layer,v->Alpha);
      }
    }
}
static void ReadFromCamera(MonitorHandle Handle,void *Data) {
    Camera cam = Data;
    char *buffer;
    int  length;

    buffer = RenderGetBuffer(cam->RenderHandle,&length);
    if(buffer == NULL) {
      printf("Error getting buffer for camera %s\n",cam->Name);
      return;
    }
    length=read(cam->StreamPipe[0],buffer,length);
    if(length <= 0) {
      printf("Read %i length something is wrong...\n",length);
      // Show no mercy to the recalcitrant child
      if(cam->Child)
        kill(cam->Child,SIGKILL);
      // Close the pipe
      close(cam->StreamPipe[0]);
      cam->StreamPipe[0] = -1;
      cam->Child = 0;
      // Make sure dont get a readcallback
      MonitorClearReadFD(Handle);
      // Set up a callback to respawn
      MonitorSetHouseKeepingTime(Handle,time(NULL) + 10);
    }
    else {
      RenderProcessBuffer(cam->RenderHandle,buffer,length,0);
    }
}
static void HouseKeepCamera(MonitorHandle Handle,void *Data) {
    Camera cam = Data;
    printf("Housekeeping for %s\n",cam->Name);
    // Why have I been called?
    if( cam->StreamPipe[0] < 0 ) {
      // Its to (re)connect to the camera
      RunStream(cam);
      // Child will be zero on error so try again later
      if(cam->Child == 0)
        MonitorSetHouseKeepingTime(Handle,time(NULL) + 10);
      else
        MonitorSetReadFD(Handle,cam->StreamPipe[0]);
    }
    else {
      printf("Dont know why HouseKeep called for %s\n",cam->Name);
    }
}
static void ReadFromLirc(MonitorHandle Handle,void *Data) {
    Plexer p = Data;
    char *code = NULL;

    if( lirc_nextcode(&code) < 0 ) {
      // The connection to lirc has probably closed
      // so shut it down properly and tell housekeep
      // to reopen it later
      close(MonitorGetReadFD(Handle));
      MonitorClearReadFD(Handle);
      MonitorSetHouseKeepingTime(Handle,time(NULL) + 10);
      return;
    }
    if(code == NULL) {
      printf("Failed to get code from lirc!!\n");
      return;
    }
    // Beyond this point code needs to freed before returning

    // The lirc code has four items seperated by a single space:
    //   KeyCode    - 64bit hexadecimal number
    //   Repeat     - hex number of repeats
    //   KeyString  - The key as a string
    //   RemoteName - The name of the remote
    // Only interested in the RemoteName and KeyString (for now)
    char *kcode,*rpt,*kstr,*rname;
    kcode = code;
    rname = strrchr(code,' ');
    if(rname) *rname++ = 0;
    kstr  = strrchr(code,' ');
    if(kstr) *kstr++ = 0;
    rpt = strrchr(code,' ');
    if(rpt) *rpt++ = 0;
    // there might be trailing CR/LF at end of rname
    for(char *s = rname; *s; s++)
      if(isspace(*s)) *s = 0;
    // Only proceed if successfully decoded
    if( !(kcode && rpt && kstr && rname) ) {
      printf("Failed to fully decode %s\n",code);
      goto endRFL;
    }
    // Find the remote control
    RemoteControl rc = NULL;
    for(int i=0; i < p->RemoteControlCount; i++) {
      if(strcmp(p->RemoteControl[i].Name,rname) == 0) {
        rc = &p->RemoteControl[i];
        break;
      }
    }
    if(rc == NULL) {
      printf("No key definitions for remote %s\n",rname);
      goto endRFL;
    }
    // Find the keystr
    int low=0,high=rc->KeyCount;
    int found = -1;
    while(low < high) {
      int check = (low+high)/2;
//      printf("%i %i %i, %s %s\n",low,check,high,rc->Key[check].Key,kstr);
      int n = strcmp(rc->Key[check].Key,kstr);
      if( n == 0 ) {
        found = check;
        break;
      }
      else if( n < 0 )
        low = check + 1;
      else
        high = check;
    }
    if(found < 0)
      goto endRFL;
    switch(rc->Key[found].OpCode) {
      case Op_None: 
        break;
      case Op_SetView:
        SetView(p,rc->Key[found].OpData1);
        break;
      case Op_NextView:
        SetView(p,(p->CurrentView+1) % p->ViewCount);
        break;
      case Op_PrevView:
        SetView(p,(p->CurrentView-1+p->ViewCount) % p->ViewCount);
        break;
      default:
        break;
    }
endRFL:
    free(code);
}
static void HouseKeepLirc(MonitorHandle Handle,void *Data) {
    printf("Housekeeping for lirc\n");
    // Why have I been called?
    if( MonitorGetReadFD(Handle) < 0 ) {
      // Its to (re)connect to lirc
      int fd = lirc_init("cctvplexer",0);
      if(fd < 0) {
        perror("Error initialising lirc");
        MonitorSetHouseKeepingTime(Handle,time(NULL) + 10);
      }
      else {
        MonitorSetReadFD(Handle,fd);
      }
    }
    else {
      printf("Dont know why HouseKeep called for lirc\n");
    }
}
static void ReadFromKeyBoard(MonitorHandle Handle,void *Data) {
    Plexer p = Data;
    int c = getchar();
    if( c >= '0' && c <= '9' ) {
      c -= '0';
      SetView(p,c);
    }
    else if(c == 'q')
      Stop++;
}
int main(int ac, char *av[]) {
    Plexer plexer;
    MonitorHandle h;
    // Want a clean stop
    if (signal(SIGINT, sighandler) == SIG_ERR) {
      printf("can't register sighandler\n");
    }
    // Get the config
    if( (plexer = LoadConfig("config.cfg")) == NULL ) {
      printf("Error loading config file %s\n","config.cfg");
      return -1;
    }
    // Initialise Display
    if(RenderInitialise()) {
      printf("Error setting up display\n");
      return -1;
    }
    // Assign each camera a render handle
    for(int i=0; i < plexer->CameraCount; i++) {
      plexer->Camera[i].RenderHandle = RenderNew(plexer->Camera[i].Name);
      if( plexer->Camera[i].RenderHandle == NULL ) {
        printf("Unable to assign render handle to %s(%i). Camera will not display\n",
                                  plexer->Camera[i].Name,i);
      }
    }
    // Set the background
    RenderSetBackground(NULL,plexer->Background);
    // Set the initial view
    SetView(plexer,0);
    // Set up the CCTV streams
    for(int i=0; i < plexer->CameraCount; i++) {
      plexer->Camera[i].StreamPipe[0] = -1;
      h = MonitorNew(plexer->Camera[i].Name);
      MonitorClearReadFD(h);
      MonitorSetReadData(h,&plexer->Camera[i]);
      MonitorSetReadCB(h,ReadFromCamera);
      MonitorSetHouseKeepingTime(h,time(NULL) + i);
      MonitorSetHouseKeepingData(h,&plexer->Camera[i]);
      MonitorSetHouseKeepingCB(h,HouseKeepCamera);
    }
    // Setup stdin for one character at a time 
    struct termios backup, raw;
    tcgetattr(STDIN_FILENO, &backup);
    cfmakeraw(&raw);
    raw.c_oflag |= OPOST | ONLCR;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    // Add a monitor for stdin
    h = MonitorNew("KeyBoard");
    MonitorSetReadData(h,plexer);
    MonitorSetReadCB(h,ReadFromKeyBoard);
    MonitorSetReadFD(h,STDIN_FILENO);
    // Add a monitor for lirc
    h = MonitorNew("LIRC");
    MonitorClearReadFD(h);
    MonitorSetReadData(h,plexer);
    MonitorSetReadCB(h,ReadFromLirc);
    MonitorSetHouseKeepingTime(h,time(NULL));
    MonitorSetHouseKeepingData(h,plexer);
    MonitorSetHouseKeepingCB(h,HouseKeepLirc);
    // Loop forever displaying the cameras
    while(Stop == 0) {
      MonitorProcess(10);
    }
    printf("Quitting...\n");
    // restore stdin
    tcsetattr(STDIN_FILENO, TCSANOW, &backup);
    // Release everything
    for(int i=0; i < plexer->CameraCount; i++) {
      RenderRelease(plexer->Camera[i].RenderHandle);
    }
    RenderDeInitialise();
    return 0;
}
