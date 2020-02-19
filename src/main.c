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
#include <signal.h>
#include <termios.h>
#include <errno.h>

#include "cctvplexer.h"
#include "render.h"

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
static void SetView(Plexer p,View view) {
    if(p == NULL || view == NULL)
      return;
    Camera c = p->Camera;
    CameraView v = view->View;
    RenderSetBackground(NULL,view->Background);
    for(int i=0; i < p->CameraCount; i++, v++, c++) {
      if( v->Visible == 0 ) {
        RendererSetInvisible(c->RenderHandle);
      }
      else if( v->FullScreen ) {
        RendererSetFullScreen(c->RenderHandle,v->KeepAspect,v->Layer,v->Alpha);
      }
      else {
        RendererSetRectangle(c->RenderHandle,v->X,v->Y,v->W,v->H,v->KeepAspect,v->Layer,v->Alpha);
      }
    }
}
int main(int ac, char *av[]) {
    Plexer plexer;

    // Want a clean stop
    if (signal(SIGINT, sighandler) == SIG_ERR) {
      printf("can't register sighandler\n");
    }
    // Get the config
    if( (plexer = LoadConfig("config.cfg")) == NULL ){
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
    // Set the initial view
    RenderSetBackground(NULL,plexer->Background);
    // Set the initial view
    SetView(plexer,&plexer->View[9]);
    // Run the streams
    for(int i=0; i < plexer->CameraCount; i++) {
      RunStream(&plexer->Camera[i]);
    }
    // Setup stdin for one character at a time
    struct termios backup, raw;
    tcgetattr(STDIN_FILENO, &backup);
    cfmakeraw(&raw);
    raw.c_oflag |= OPOST | ONLCR;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    // Loop forever displaying the cameras
    while(Stop == 0) {
      fd_set readfds;
      int  maxfd = 0;
      FD_ZERO(&readfds);
      for(int i = 0; i < plexer->CameraCount; i++) {
        if(plexer->Camera[i].StreamPipe[0]) {
          FD_SET(plexer->Camera[i].StreamPipe[0],&readfds);
          maxfd = plexer->Camera[i].StreamPipe[0] > maxfd ? plexer->Camera[i].StreamPipe[0] : maxfd;
        }
      }
      FD_SET(STDIN_FILENO,&readfds);
      if(select(maxfd+1,&readfds,NULL,NULL,NULL) < 0) {
        printf("Select error: %s\n",strerror(errno));
        continue;
      }
      // Process any incoming data
      for(int i = 0; i < plexer->CameraCount; i++) {
        if(FD_ISSET(plexer->Camera[i].StreamPipe[0],&readfds)) {
          char *buffer;
          int32_t length=0;
          buffer = RenderGetBuffer(plexer->Camera[i].RenderHandle,&length);
          if(buffer == NULL) {
            printf("Error getting buffer for camera\n");
            exit(0);
            continue;
          }
          length=read(plexer->Camera[i].StreamPipe[0],buffer,length);
          if(length <= 0) {
            printf("Read %i length something is wrong...\n",length);
          }
          else {
            RenderProcessBuffer(plexer->Camera[i].RenderHandle,buffer,length,0);
          }
        }
      }
      // Read STDIN for commands (Temporary)
      if(FD_ISSET(STDIN_FILENO,&readfds)) {
        int c = getchar();
        if( c >= '0' && c <= '9' ) {
          c -= '0';
          if(c < plexer->ViewCount)
            SetView(plexer,&plexer->View[c]);
        }
        if(c == 'q')
          break;
      }
    }
    // restore stdin
    tcsetattr(STDIN_FILENO, TCSANOW, &backup);
    return 0;
}
