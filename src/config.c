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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libconfig.h>
#include <math.h>
#include "cctvplexer.h"

#define MAX_PATH_SIZE     256
#define INDEX_NAME  "IndexBLahBlah"
// If (va) isn't a number use (de). Use (va)/(sc) if sc is number otherwise use (va)
#define SCALE_OR_VALUE(sc,va,de) ( (va) != (va) ? (de) : (sc) == (sc) ? (va)/(sc) : (va) )

// Default values
static struct _CameraView DefaultView = {
    .X          = 0.25,         // Left
    .Y          = 0.25,         // Top
    .W          = 0.10,         // Width
    .H          = 0.10,         // Height
    .Alpha      = 1.0,          // Opaque
    .Layer      = 1,            // On Layer 1
    .FullScreen = 0,            // Not fullscreen
    .KeepAspect = 0,            // Dont preserve aspect
    .Visible    = 0,            // Invisible
};
//
// Count the children and add a unique
// incrementing index starting at 0.
// The count is used to allocate memory and
// the index for finding the allocated object
// when it has been referenced in another
// part of the config.
//
static int CountAndIndexChildren(config_t *cfg,config_setting_t *setting ) {
    config_setting_t *child;
    config_setting_t *index;

    int count;
    if(setting == NULL)
      return 0;
    count = 0;
    while((child = config_setting_get_elem(setting,count)) != NULL) {
      // Remove old index just in case it exists
      config_setting_remove(child,INDEX_NAME);
      // Add the index
      index = config_setting_add(child,INDEX_NAME,CONFIG_TYPE_INT);
      if( !(index && config_setting_set_int(index,count)) ) {
        printf("Possible config error. Unable to add index %s to %s:%s\n",
                  INDEX_NAME,config_setting_name(setting),config_setting_name(child));
      }
      count++;
    }
    return count;
}

// Get the index number added in CountAndIndexChildren
static int GetIndex(config_setting_t *setting,const char *item) {
    config_setting_t *itemcfg;
    int index;
    if(setting == NULL || item == NULL)
      return -1;
    itemcfg = config_setting_lookup(setting,item);
    if(config_setting_lookup_int(itemcfg,INDEX_NAME,&index)) {
      return index;
    }
    printf("Failed to find index %s in %s.%s\n",INDEX_NAME,config_setting_name(setting),item);
    return -1;
}
//
// Builds up a string that references other parts
// of the config. Some strings can include a
// reference to another string using ${PATHTOSTRING}
//
static char *StringBuilder(config_t *cfg,const char *string) {
    static char output[MAX_PATH_SIZE];
    char path[MAX_PATH_SIZE];
    int len=0;
    while(*string && len < (MAX_PATH_SIZE-1)) {
      // Check if it is an escaped $ ($$)
      if( *string == '$' && *(string+1) == '$' ) {
        string++;
        output[len++] = *string++;
        continue;
      }
      // Check if it is a reference to config path
      if( *string == '$' && *(string+1) == '{' ) {
        char *end = strchr(string,'}');
        if(end == NULL) 
          return NULL;        // Calling function will raise the error
        // Skip the ${ 
        string += 2;
        if( (end - string) >= MAX_PATH_SIZE )
          return NULL;
        // Copy into lookup
        strncpy(path,string,end-string);
        path[end-string] = 0;
        // Get the string value from the path
        const char *strval = NULL;
        config_lookup_string(cfg,path,&strval);
        // Copy it into the output
        if(strval) {
          while(*strval && len < (MAX_PATH_SIZE-1))
            output[len++] = *strval++;
          // If there is anything left then string is to long
          if( *strval )
            return NULL;
        }
        else {
          printf("Lookup of %s failed\n",path);
        }
        // Move to end of reference
        string = end + 1;
        continue;
      }
      output[len++] = *string++;
    }
    output[len] = 0;
    return output;
}
static int LoadCameras(Plexer plx,config_t *cfg,config_setting_t *cameras) {
    // CAMERAS
    // Count the cameras so space can be allocated
    plx->CameraCount = CountAndIndexChildren(cfg,cameras);
    // Allocate space for the cameras
    plx->Camera = calloc(plx->CameraCount,sizeof(struct _Camera));
    // Configure them
    for(int i=0; i < plx->CameraCount; i++) {
      int  intval;
      const char *strval;
      // Get the i'th camera
      config_setting_t *c = config_setting_get_elem(cameras,i);
      // Save its settings
      if( config_setting_lookup_string(c,"FriendlyName",&strval) )
        plx->Camera[i].Name = strdup(strval);
      else
        plx->Camera[i].Name = strdup(config_setting_name(c));
//      printf("CAMERA(%i): %s\n",i,plx->Camera[i].Name);
      plx->Camera[i].ControlID = config_setting_lookup_int(c, "PTZID", &intval) ? intval : 0;
      // Settings for the the stream
      // Count the arguments so enough pointers can be allocated
      config_setting_t *args = config_setting_lookup(c,"Arguments");
      int ac;
      for(ac = 0; config_setting_get_elem(args,ac); ac++)
        ;
      // Add 1 for the command and 1 for the terminating NULL
      ac += 2;
      // Fetch the command
      const char *command;
      if(!config_setting_lookup_string(c,"Command",&command)) {
        printf("No stream command for camera %s\n",plx->Camera[i].Name);
        continue;
      }
      plx->Camera[i].StreamCommand = calloc(ac,sizeof(char *));
      plx->Camera[i].StreamCommand[0] = strdup(command);
      ac = 0;
      do {
        const char *argument;
        if( (argument=config_setting_get_string_elem(args,ac)) != NULL ) {
          char *s = StringBuilder(cfg,argument);
          plx->Camera[i].StreamCommand[ac+1] = s ? strdup(s) : NULL;
        }
        else
          plx->Camera[i].StreamCommand[ac+1] = NULL;
        ac++;
      } while(plx->Camera[i].StreamCommand[ac]);
    }
    return 1;
}
static int LoadViews(Plexer plx,config_t *cfg,config_setting_t *views,config_setting_t *cset) {
    plx->ViewCount = CountAndIndexChildren(cfg,views);
    // Allocate space for the views
    plx->View =  calloc(plx->ViewCount,sizeof(struct _View));
    // Allocate space for each camera view within the views
    // and set them to their default values
    for(int i=0; i < plx->ViewCount; i++) {
      plx->View[i].View = calloc(plx->CameraCount,sizeof(struct _CameraView));
      for(int j = 0; j < plx->CameraCount; j++) {
        memcpy(&plx->View[i].View[j],&DefaultView,sizeof(struct _CameraView));
      }
      plx->View[i].Background = plx->Background;
    }
    // Configure each view
    for(int i=0; i < plx->ViewCount; i++) {
      config_setting_t *v = config_setting_get_elem(views,i);
      char *viewname = config_setting_name(v);
      config_setting_lookup_int(v,"Background",&plx->View[i].Background);
      // Get the Cameras array
      config_setting_t *cv = config_setting_lookup(v,"Cameras");
      if(cv == NULL) {
        printf("Warning: %s:%s has no camera views defined\n","View",viewname);
        continue;
      }
      config_setting_t *c;
      int j = 0;
      while( (c = config_setting_get_elem(cv,j)) != NULL) {
        j++;
        // Get the Camera this applies to
        const char *camera;
        if(!config_setting_lookup_string(c,"Camera",&camera)) {
          printf("No camera in item %i of %s:%s, ignoring\n",j,"View",viewname);
          continue;
        }
        // Get the index from the camera name
        int camidx = GetIndex(cset,camera);
        if(camidx < 0) {
          printf("Could not find camera %s used in item %i of %s:%s, ignoring\n",
                          camera,j,"View",viewname);
          continue;
        }
        CameraView camv = &plx->View[i].View[camidx];       // Just for convenience
        // The cameras presence in a view implies its is visible
        // this can be overidden with one of th attributes
        camv->Visible = 1;
        // Position and Size can be overruled by local values
        double scalex=NAN,scaley=NAN,x=NAN,y=NAN,w=NAN,h=NAN;
        config_setting_lookup_float(c,"ScaleX",&scalex);
        config_setting_lookup_float(c,"ScaleY",&scaley);
        config_setting_lookup_float(c,"X",&x);
        config_setting_lookup_float(c,"Y",&y);
        config_setting_lookup_float(c,"Width",&w);
        config_setting_lookup_float(c,"Height",&h);
        camv->X = SCALE_OR_VALUE(scalex,x,camv->X);
        camv->Y = SCALE_OR_VALUE(scaley,y,camv->Y);
        camv->W = SCALE_OR_VALUE(scalex,w,camv->W);
        camv->H = SCALE_OR_VALUE(scaley,h,camv->H);
        // Transpancy (Alpha)
        double alpha=NAN;
        config_setting_lookup_float(c,"Alpha",&alpha);
        if(alpha == alpha)
          camv->Alpha = alpha;
        // Layer
        int value;
        if(config_setting_lookup_int(c,"Layer",&value))
          camv->Layer = value;
        // Fullscreen
        if(config_setting_lookup_bool(c,"Fullscreen",&value))
          camv->FullScreen = value;
        // Visible
        if(config_setting_lookup_bool(c,"Visible",&value))
          camv->Visible = value;
      }
    }
    return 1;
}
// Load the config
Plexer LoadConfig(char *file) {
    config_t cfg;
    Plexer plexer;

    config_init(&cfg);

    /* Read the file. If there is an error, report it and exit. */
    if(! config_read_file(&cfg, file)) {
      fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
                  config_error_line(&cfg), config_error_text(&cfg));
      config_destroy(&cfg);
      return NULL;
    }

    // Want to be able to read integers as floats
    config_set_options(&cfg,CONFIG_OPTION_AUTOCONVERT);
    // Allocate memory for the plexer
    plexer = calloc(1,sizeof(struct _Plexer));
    // Default Background colour
    config_lookup_int(&cfg,"Background",&plexer->Background);

    // CAMERAS
    config_setting_t *cams = config_lookup(&cfg,"Camera");
    LoadCameras(plexer,&cfg,cams);
    // VIEWS
    config_setting_t *views = config_lookup(&cfg,"View");
    LoadViews(plexer,&cfg,views,cams);

//    config_write_file(&cfg, "config.new");
    config_destroy(&cfg);
    return plexer;
}
