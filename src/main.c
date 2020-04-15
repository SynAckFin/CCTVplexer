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

#include "cctvplexer.h"
#include "render.h"
#include "monitor.h"
int Stop = 0;
extern CURLM *CurlHandle;

static void sighandler(int iSignal) {
  printf("signal caught: %d - exiting\n", iSignal);
  if(Stop >= 3) exit(0);
  Stop++;
}
static size_t CurlWrite(char *ptr,size_t size, size_t nmemb, void *userdata) {
    return size * nmemb;
}
static void PTZOperation(Plexer p,KeyMap k) {
    PTZController ptz;

    if(p->Focus == NULL || p->Focus->PTZ == NULL)
      return;
    ptz = p->Focus->PTZ;
    if( k->OpCode >= Op_PTZ_Max)
      return;
    if(ptz->Control[k->OpCode].URL == NULL)
      return;

    CURL *easy = curl_easy_init( );
    char buffer[1024];
    snprintf(buffer,1024,ptz->Control[k->OpCode].URL,
             k->OpData1,k->OpData2,k->OpData3,k->OpData4,k->OpData5);
    curl_easy_setopt(easy,CURLOPT_URL,buffer);
    curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,CurlWrite);
    switch(ptz->Control[k->OpCode].Method) {
      case HTTP_DELETE:
        curl_easy_setopt(easy,CURLOPT_CUSTOMREQUEST,"DELETE");
      case HTTP_GET: break;
      case HTTP_PUT:
        curl_easy_setopt(easy,CURLOPT_CUSTOMREQUEST,"PUT");
      case HTTP_POST:
        if(ptz->Control[k->OpCode].Content)
          snprintf(buffer,1024,ptz->Control[k->OpCode].Content,
              k->OpData1,k->OpData2,k->OpData3,k->OpData4,k->OpData5);
        else {
          buffer[0] = 0;
        }
        curl_easy_setopt(easy,CURLOPT_COPYPOSTFIELDS,buffer);
        break;
      default: break;
    }
    curl_multi_add_handle(CurlHandle,easy);
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
    if(p->View[view].Focus)
      p->Focus = p->View[view].Focus;
    RenderSetBackgroundColour(NULL,p->View[view].BackgroundColour);
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
static void ReadImage(MonitorHandle Handle,void *Data) {
    char *buffer;
    int  length,maxlength;

    buffer = RenderGetBuffer(Data,&maxlength);
    if(buffer == NULL) {
      printf("Error getting buffer for image\n");
      return;
    }
    length=read(Handle->FileDescriptor,buffer,maxlength);
    if(length <= 0) {
      close(Handle->FileDescriptor);
      MonitorClearReadFD(Handle);
      memset(buffer,0,maxlength);
      RenderProcessBuffer(Data,buffer,0,1);
    }
    else {
      RenderProcessBuffer(Data,buffer,length,length < maxlength ? 1 : 0);
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
    char *lirccode = NULL;

    if( lirc_nextcode(&lirccode) < 0 ) {
      // The connection to lirc has probably closed
      // so shut it down properly and tell housekeep
      // to reopen it later
      close(MonitorGetReadFD(Handle));
      MonitorClearReadFD(Handle);
      MonitorSetHouseKeepingTime(Handle,time(NULL) + 10);
      return;
    }
    if(lirccode == NULL) {
      printf("Failed to get code from lirc!!\n");
      return;
    }
    // Copy and free lirccode so there is no need to keep track
    char code[256];                 // Should really use a define for this
    strncpy(code,lirccode,256);
    code[255] = 0;
    free(lirccode);

    // The lirc code has four items seperated by a single space:
    //   KeyCode    - 64bit hexadecimal number
    //   Repeat     - hex number of repeats
    //   KeyString  - The key as a string
    //   RemoteName - The name of the remote
    char *field1,*field2,*field3,*field4,*endstr;
    field1 = code;
    field2 = field1 ? strchr(field1,' ')   : NULL;
    field3 = field2 ? strchr(field2+1,' ') : NULL;
    field4 = field3 ? strchr(field3+1,' ') : NULL;
    endstr = field4 ? strpbrk(field4+1,"\n\r\f\t\v ") : NULL;
    if( !(field1 && field2 && field3 && field4) ) {
      printf("Unable to interpret lirc code %s\n",code);
      return;
    }
    // Terminate the fields
    *field2++ = *field3++ = *field4++ = 0;
    if(endstr)
      *endstr = 0;
    // Extract the data
    uint64_t keycode = strtoll(field1,NULL,16);
    uint32_t repeat  = strtol(field2,NULL,16);
    char    *key     = field3;
    char    *remote  = field4;
    (void) keycode;               // Avoid warnings
    // Find the remote control
    RemoteControl rc = NULL;
    for(int i=0; i < p->RemoteControlCount; i++) {
      if(strcmp(p->RemoteControl[i].Name,remote) == 0) {
        rc = &p->RemoteControl[i];
        break;
      }
    }
    if(rc == NULL) {
      printf("No key definitions for remote %s\n",remote);
      return;
    }
    // Find the keystr
    int low=0,high=rc->KeyCount;
    int found = -1;
    while(low < high) {
      int check = (low+high)/2;
      // printf("%i %i %i, %s %s\n",low,check,high,rc->Key[check].Key,key);
      int n = strcmp(rc->Key[check].Key,key);
      // Compare the repeatcount if necessary
      n = n || rc->Key[check].RepeatCount == -1 ? n : rc->Key[check].RepeatCount - repeat;
      if( n == 0 ) {
        found = check;
        break;
      }
      else if( n < 0 )
        low = check + 1;
      else
        high = check;
    }
    if(found < 0) {
//      printf("Unhandled key %s with remote %s\n",key,remote);
      return;
    }
    // What to with it
    if( rc->Key[found].OpCode > Op_PTZ_None && rc->Key[found].OpCode < Op_PTZ_Max) {
      PTZOperation(p,&rc->Key[found]);
      return;
    }
    switch(rc->Key[found].OpCode) {
      case Op_None:
      case Op_PTZ_Max:
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
      case Op_Quit:
        Stop = 1;
        break;
      default:
        printf("Opcode %i not implemented\n",rc->Key[found].OpCode);
        break;
    }
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
    char  inbuf[BUFSIZ];
    int   n;
    n = read(0,inbuf,BUFSIZ);
    if(n > 1) {
      for(int i=0; i<(n-1); i++)
        printf("0x%02x,",inbuf[i]);
      printf("0x%02x\n",inbuf[n-1]);
    }

    if( inbuf[0] >= '0' && inbuf[0] <= '9' ) {
      inbuf[0] -= '0';
      SetView(p,inbuf[0]);
    }
    else if(inbuf[0] == 'q')
      Stop++;
}
int main(int ac, char *av[]) {
    Plexer plexer;
    MonitorHandle h,bgimage;
    void *r;
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
    // Initialiase FD monitoring
    MonitorInitialise();
    // Assign each camera a render handle
    for(int i=0; i < plexer->CameraCount; i++) {
      plexer->Camera[i].RenderHandle = RenderNew(plexer->Camera[i].Name,0);
      if( plexer->Camera[i].RenderHandle == NULL ) {
        printf("Unable to assign render handle to %s(%i). Camera will not display\n",
                                  plexer->Camera[i].Name,i);
      }
    }
    // Set up the background colour
    RenderSetBackgroundColour(NULL,plexer->BackgroundColour);
    // Set up the background image
    r = RenderSetBackgroundImage(NULL,NULL);
    bgimage = MonitorNew("BackgroundImage");
    MonitorClearReadFD(bgimage);
    MonitorSetReadData(bgimage,r);
    MonitorSetReadCB(bgimage,ReadImage);
    // Use it
    int fd;
    if( (fd = open(plexer->BackgroundImage,O_RDONLY)) < 0 ) {
      printf("Error opening image %s: %s\n",plexer->BackgroundImage,strerror(errno));
    }
    else {
      MonitorSetReadFD(bgimage,fd);
    }
    // Set the initial view
    SetView(plexer,0);
    // Set up the CCTV streams
    for(int i=0; i < plexer->CameraCount; i++) {
      plexer->Camera[i].StreamPipe[0] = -1;
      h = MonitorNew(plexer->Camera[i].Name);
      MonitorClearReadFD(h);
      MonitorSetReadData(h,&plexer->Camera[i]);
      MonitorSetReadCB(h,ReadFromCamera);
      MonitorSetHouseKeepingTime(h,time(NULL) + i*3);
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
