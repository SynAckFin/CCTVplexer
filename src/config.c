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

#define WARN(cfg,fmt,...)   do { \
  printf("WARNING: %s(%i): " fmt,config_setting_source_file(cfg), \
                                 config_setting_source_line(cfg)  \
                                 __VA_OPT__(,)__VA_ARGS__); } while(0)

#define MAX_PATH_LENGTH     256
#define MAX_STRING_LENGTH   1024
#define MAX_PARSE_COUNT     10

#define INDEX_NAME  "IndexBLahBlah"
// If (va) isn't a number use (de) else use (va)/(sc) if sc is number otherwise use (va)
#define SCALE_OR_VALUE(sc,va,de) ( (va) != (va) ? (de) : (sc) == (sc) ? (va)/(sc) : (va) )
#define NKEYWORDS  (sizeof(Keywords)/sizeof(Keywords[0]))
static char *Keywords[] = { "Speed", "Preset", "Pan", "Tilt", "Zoom" };

static void DumpCamera(Camera cam) {
    printf("================================\n");
    if(cam == NULL) {
      printf("NULL CAMERA!!!\n");
      return;
    }
    printf("Name: %s\n",cam->Name);
    if( cam->StreamCommand ) {
      printf("Command:");
      for(int i=0; cam->StreamCommand[i] ; i++)
        printf(" %s\n",cam->StreamCommand[i]);
      printf("\n");
    }
    if(cam->PTZ) {
      printf("Control: %s\n",cam->PTZ->Name);
      for(int i=0; i < Op_PTZ_Max; i++) {
        printf("%i:\n",i);
        printf("  %i\n",cam->PTZ->Control[i].Method);
        printf("  %s\n",cam->PTZ->Control[i].URL);
        printf("  %s\n",cam->PTZ->Control[i].ContentType);
        printf("  %s\n",cam->PTZ->Control[i].Content);
      }
    }

}

// Default values
static struct _CameraView DefaultView = {
    .X          = 0.25,         // Left
    .Y          = 0.25,         // Top
    .W          = 0.10,         // Width
    .H          = 0.10,         // Height
    .Alpha      = 1.0,          // Opaque
    .Layer      = 1,            // On Layer 1
    .FullScreen = 0,            // Not fullscreen
    .KeepAspect = 0,            // Preserve aspect
    .Visible    = 0,            // Invisible
};
//
// Count the children and add a unique incrementing index starting at 0.
// The count is used to allocate memory and the index for finding the
// allocated object when it has been referenced in another part of the
// config. Returns count
//
static int CountAndIndexChildren(config_setting_t *setting ) {
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
// Get the index number of item in setting. Index added in CountAndIndexChildren
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
// Get index of item in path. Index was added in CountAndIndexChildren
static int GetIndexByPath(config_t *cfg,const char *path,const char *item) {
    config_setting_t *setting = config_lookup(cfg,path);
    if(setting == NULL) {
      printf("Path %s does not exist in config files\n",path);
      return -1;
    }
    return GetIndex(setting,item);
}
// Sort function for sorting keymaps
static int KeyCompare(const void *k1,const void *k2) {
    int n = strcmp(((KeyMap) k1)->Key,((KeyMap) k2)->Key);
    return n ? n : ((KeyMap) k1)->RepeatCount - ((KeyMap) k2)->RepeatCount;
}
// Convert a string into an opcode
static OpCode StringToOpcode(config_setting_t *Setting,const char *Action) {
    if( strcmp(Action,"ViewNext") == 0 )
      return Op_NextView;
    if( strcmp(Action,"ViewPrev") == 0 )
      return Op_PrevView;
    if( strcmp(Action,"View") == 0 )
      return Op_SetView;
    if( strcmp(Action,"Quit") == 0 )
      return Op_Quit;
    if( strcmp(Action,"PTZ_Up") == 0 )          return Op_PTZ_Up;
    if( strcmp(Action,"PTZ_UpRight") == 0 )     return Op_PTZ_UpRight;
    if( strcmp(Action,"PTZ_Right") == 0 )       return Op_PTZ_Right;
    if( strcmp(Action,"PTZ_DownRight") == 0 )   return Op_PTZ_DownRight;
    if( strcmp(Action,"PTZ_Down") == 0 )        return Op_PTZ_Down;
    if( strcmp(Action,"PTZ_DownLeft") == 0 )    return Op_PTZ_DownLeft;
    if( strcmp(Action,"PTZ_Left") == 0 )        return Op_PTZ_Left;
    if( strcmp(Action,"PTZ_UpLeft") == 0 )      return Op_PTZ_UpLeft;
    if( strcmp(Action,"PTZ_Stop") == 0 )        return Op_PTZ_Stop;
    if( strcmp(Action,"PTZ_ZoomIn") == 0 )      return Op_PTZ_ZoomIn;
    if( strcmp(Action,"PTZ_ZoomOut") == 0 )     return Op_PTZ_ZoomOut;
    if( strcmp(Action,"PTZ_FocusNear") == 0 )   return Op_PTZ_FocusNear;
    if( strcmp(Action,"PTZ_FocusFar") == 0 )    return Op_PTZ_FocusFar;
    if( strcmp(Action,"PTZ_IrisClose") == 0 )   return Op_PTZ_IrisClose;
    if( strcmp(Action,"PTZ_IrisOpen") == 0 )    return Op_PTZ_IrisOpen;
    if( strcmp(Action,"PTZ_GotoPreset") == 0 )  return Op_PTZ_GotoPreset;
    if( strcmp(Action,"PTZ_SetPreset") == 0 )   return Op_PTZ_SetPreset;
    if( strcmp(Action,"PTZ_ClearPreset") == 0 ) return Op_PTZ_ClearPreset;
    WARN(Setting,"Unknown Action %s\n",Action);
    return Op_None;
}
//
// Builds up a string that references other parts of the config
//   ${ROOTPATH} - Root path to another value
//   $(RELPATH)  - Path relative to "context"
//   $[KEYNAME]  - Special Keyname. Only useful in PTZ definitions
//
static char *StringParser(config_t *cfg,config_setting_t *context,const char *instring) {
    static char output[MAX_STRING_LENGTH];
    char input[MAX_STRING_LENGTH];

    // The strings can reference other strings within the
    // config and they, in turn, can reference other strings
    // Because of this the string is parsed until it doesn't
    // change. Its is also possible for an infinit loop to
    // occur by having a string self reference. To avoid this
    // the string will get parsed MAX_PARSE_COUNT times before
    // giving up
    strncpy(input,instring,MAX_STRING_LENGTH);
    input[MAX_STRING_LENGTH-1] = 0;
    output[0] = 0;
    for(int pcnt=0; pcnt < MAX_PARSE_COUNT; pcnt++) {
      int outlen = 0;
      char *inptr = input;
      while(*inptr && outlen < (MAX_STRING_LENGTH-1)) {
        // Check if it is an escaped $ (\$) or % (\%) 
        if( *inptr == '\\' && *(inptr+1) == '$' ) {
          inptr++;
          output[outlen++] = *inptr++;
          continue;
        }
        // Check if it is a rootpath reference
        if( *inptr == '$' && *(inptr+1) == '{' ) {
          char *end = strchr(inptr,'}');
          if(end == NULL) {
            WARN(context,"Bad root path reference parsing \"%s\"\n",instring);
            // Perhaps it is intentional
            output[outlen++] = *inptr++;
            continue;
          }
          *end = 0;
          const char *substring = NULL;
          inptr += 2;
          config_lookup_string(cfg,inptr,&substring);
          if(substring == NULL) {
            WARN(context,"Unable to find string at %s\n",inptr);
          }
          else {
            // Copy the substring
            while(*substring && outlen < (MAX_STRING_LENGTH-1))
              output[outlen++] = *substring++;
          }
          // Restore the '}' and skip past it
          *end = '}';
          inptr = end+1;
          continue;
        }
        // Check if it is a context reference
        if( *inptr == '$' && *(inptr+1) == '(' ) {
          char *end = strchr(inptr,')');
          if(end == NULL) {
            WARN(context,"Bad root path reference parsing \"%s\"\n",instring);
            // Perhaps it is intentional
            output[outlen++] = *inptr++;
            continue;
          }
          *end = 0;
          const char *substring = NULL;
          inptr += 2;
          config_setting_lookup_string(context,inptr,&substring);
          if(substring == NULL) {
            WARN(context,"Unable to find setting %s in %s\n",inptr,config_setting_name(context));
          }
          else {
            // Copy the substring
            while(*substring && outlen < (MAX_STRING_LENGTH-1))
              output[outlen++] = *substring++;
          }
          // Restore the '}' and skip past it
          *end = ')';
          inptr = end+1;
          continue;
        }
        // Check if it is a keyword reference
        if( *inptr == '$' && *(inptr+1) == '[' ) {
          char *end = strchr(inptr,']');
          if(end == NULL) {
            WARN(context,"Bad root path reference parsing \"%s\"\n",instring);
            // Perhaps it is intentional
            output[outlen++] = *inptr++;
            continue;
          }
          // Temporary terminate
          *end = 0;
          // Generate substring based on keyword
          char substring[20] = {0};
          for(int i=0; i < NKEYWORDS; i++) {
            if(strcmp(inptr+2,Keywords[i]) == 0) {
              snprintf(substring,20,"%%%i$i",i+1);
              break;
            }
          }
          // Check a substring was found
          if(substring[0] == 0) {
            WARN(context,"Keyword %s not found.\n",inptr+2);
            output[outlen++] = *inptr++;
            *end = ']';
            continue;
          }
          else {
            // Copy the substring
            for(int i=0; substring[i]; i++)
              output[outlen++] = substring[i];
          }
          // Restore the '}' and skip past it
          *end = ']';
          inptr = end+1;
          continue;
        }
        output[outlen++] = *inptr++;
      }
      output[outlen] = 0;
      if(strcmp(output,input) == 0)
        break;
      // Copy the output to inut and reparse
      strcpy(input,output);
    }
    return output;

    return NULL;
}
static PTZController LoadPTZ(config_t *cfg,config_setting_t *context,config_setting_t *ptz) {
    if(ptz == NULL)
      return NULL;
    config_setting_t *controls = config_setting_lookup(ptz,"Controls");
    if(controls == NULL) {
      WARN(ptz,"PTZ controller %s has no controls defined.\n",config_setting_name(ptz));
      return NULL;
    }

    PTZController pc = calloc(1,sizeof(struct _PTZController));
    pc->Name = strdup(config_setting_name(ptz));
    // Loop through the controls
    config_setting_t *ctrl ;
    for(int idx = 0; (ctrl = config_setting_get_elem(controls,idx)) != NULL; idx++) {
      OpCode op = StringToOpcode(ctrl,config_setting_name(ctrl));
      if(op == Op_None || op >= Op_PTZ_Max) {
        WARN(ctrl,"Unknown action %s\n",config_setting_name(ctrl));
        continue;
      }
      const char *argument = NULL;
      if(config_setting_lookup_string(ctrl,"URL",&argument)) {
        pc->Control[op].URL = strdup(StringParser(cfg,context,argument));
      }
      if(config_setting_lookup_string(ctrl,"Method",&argument)) {
        pc->Control[op].Method = strcasecmp(argument,"POST")   == 0 ? HTTP_POST :
                                 strcasecmp(argument,"PUT")    == 0 ? HTTP_PUT :
                                 strcasecmp(argument,"DELETE") == 0 ? HTTP_DELETE : HTTP_GET;
      }
      else {
        pc->Control[op].Method = HTTP_GET;
      }
      if(config_setting_lookup_string(ctrl,"Content",&argument)) {
        pc->Control[op].Content = strdup(StringParser(cfg,context,argument));
      }
      if(config_setting_lookup_string(ctrl,"ContentType",&argument)) {
        pc->Control[op].ContentType = strdup(StringParser(cfg,context,argument));
      }
    }
    return pc;
}
static int LoadCameras(Plexer plx,config_t *cfg,config_setting_t *camdefs) {
    // Get the PTZ interfaces settings (might not exist)
    config_setting_t *ptzdefs = config_lookup(cfg,"PTZController");
    // CAMERAS
    // Count the cameras so space can be allocated
    plx->CameraCount = CountAndIndexChildren(camdefs);
    // Allocate space for the cameras
    plx->Camera = calloc(plx->CameraCount,sizeof(struct _Camera));
    // Configure them
    for(int i=0; i < plx->CameraCount; i++) {
      // Get the configuration definition
      config_setting_t *camera = config_setting_get_elem(camdefs,i);
      // Get the parameters
      const char *ptzcontroller = NULL;
      const char *command = NULL;
      const char *name = NULL;
      config_setting_t *comargs = NULL;

      config_setting_lookup_string(camera,"FriendlyName",&name);
      config_setting_lookup_string(camera,"PTZController",&ptzcontroller);
      config_setting_lookup_string(camera,"Command",&command);
      comargs = config_setting_lookup(camera,"Arguments");

      //Stream command and arguments
      if(command == NULL) {
        WARN(camera,"No stream command for camera %s.\n",config_setting_name(camera));
        comargs = NULL;  // No command, no arguments
      }
      else {
        int argcount = comargs ? config_setting_length(comargs) : 0;
        // Add 1 for the command and 1 for terminating NULL
        argcount += 2;
        plx->Camera[i].StreamCommand = calloc(argcount,sizeof(char *));
        plx->Camera[i].StreamCommand[0] = strdup(command);
        for(int idx = 0; comargs && idx < config_setting_length(comargs); idx++) {
          const char *argument = config_setting_get_string_elem(comargs,idx);
          char *s = StringParser(cfg,camera,argument);
          plx->Camera[i].StreamCommand[idx+1] = s ? strdup(s) : NULL;
        }
      }
      // PTZ control
      if(ptzcontroller) {
        config_setting_t *control = ptzdefs ? config_setting_lookup(ptzdefs,ptzcontroller) : NULL;
        if(control == NULL) {
          WARN(camera,"Could not find defintion %s for PTZ control\n",config_setting_name(camera));
        }
        else {
          plx->Camera[i].PTZ = LoadPTZ(cfg,camera,control);
        }
      }
      // Camera name
      plx->Camera[i].Name = strdup(name ? name : config_setting_name(camera));
//      DumpCamera(&plx->Camera[i]);
    }
    return 1;
}
static int LoadViews(Plexer plx,config_t *cfg,config_setting_t *views,config_setting_t *cset) {
    plx->ViewCount = CountAndIndexChildren(views);
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
      const char *focus;
      if(config_setting_lookup_string(v,"Focus",&focus)) {
        int camidx = GetIndex(cset,focus);
        if(camidx >= 0) {
          plx->View[i].Focus = &plx->Camera[camidx];
        }
      }
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
static int LoadKeyMaps(Plexer plx,config_t *cfg,config_setting_t *kmaps) {
    // Dont need to index but do need the count
    plx->RemoteControlCount = config_setting_length(kmaps);
    printf("Found %i remote control configs\n",plx->RemoteControlCount);
    if(plx->RemoteControlCount == 0)
      return 1;
    // Allocate space for the Remote Controls
    plx->RemoteControl = calloc(plx->RemoteControlCount,sizeof(struct _RemoteControl));
    // Load each remotes config
    for(int i=0; i < plx->RemoteControlCount;i++) {
      RemoteControl rc = &plx->RemoteControl[i];
      config_setting_t *remote = config_setting_get_elem(kmaps,i);
      if(remote == NULL) {
        // NOTE: Could this be dangerous - look into it
        printf("Failed getting remote config\n");
        continue;
      }
      rc->Name = strdup(config_setting_name(remote));
      rc->KeyCount = config_setting_length(remote);
      rc->Key = calloc(rc->KeyCount,sizeof(struct _KeyMap));
      printf("Remote %s has %i key definitions\n",rc->Name,rc->KeyCount);
      // Load each key
      for(int k = 0; k < rc->KeyCount; k++) {
        config_setting_t *key = config_setting_get_elem(remote,k);
        if(key == NULL) {
          // Could be dangerous
          printf("Failed to get key\n");
          continue;
        }
        // Each element has:
        //   Keycode   =  The keycode from lirc
        //   Repeat    = (optional) the repeat number from lirc
        //   Action    = The action to take
        //   ...       = Depends upong the action
        const char *keycode = NULL;
        int  repeat = -1;
        const char *action = NULL;
        config_setting_lookup_string(key,"KeyCode",&keycode);
        config_setting_lookup_int(key,"Repeat",&repeat);
        config_setting_lookup_string(key,"Action",&action);
//        printf("Key: %s, repeat %i, Action %s\n",keycode,repeat,action);
        if(keycode == NULL) {
          WARN(key,"KeyCode not found\n");
          // Need keycode set to something...
          keycode = "NULL";
        }
        // keycode+repeat should be unique but it isn't
        // catastrophic if they aren't, it will just
        // lead to undefined behaviour.
        // Check for uniqueness
        for(int i=0; i < k; i++) {
          if(strcmp(rc->Key[i].Key,keycode) == 0 && rc->Key[i].RepeatCount == repeat) {
            WARN(key,"KeyCode %s with repeat %i is not unique.\n",keycode,repeat);
            break;
          }
        }
        rc->Key[k].Key = strdup(keycode);
        rc->Key[k].RepeatCount = repeat;
        rc->Key[k].OpCode = StringToOpcode(key,action);
        rc->Key[k].OpData1 = 0;
        rc->Key[k].OpData2 = 0;
        rc->Key[k].OpData3 = 0;
        rc->Key[k].OpData4 = 0;
        rc->Key[k].OpData5 = 0;
        config_setting_lookup_int(key,"Speed",  &rc->Key[k].OpData1);
        config_setting_lookup_int(key,"Preset", &rc->Key[k].OpData2);
        config_setting_lookup_int(key,"Pan",    &rc->Key[k].OpData3);
        config_setting_lookup_int(key,"Tilt",   &rc->Key[k].OpData4);
        config_setting_lookup_int(key,"Zoom",   &rc->Key[k].OpData5);
        // Populate other arguments based upon the OpCode
        switch(rc->Key[k].OpCode) {
          case Op_None:
          case Op_NextView:
          case Op_PrevView:
          case Op_PTZ_Stop:
            break;
          case Op_SetView: {
            const char *viewname = NULL;
            rc->Key[k].OpData1 = 0;         // Default value
            config_setting_lookup_string(key,"ViewName",&viewname);
            if(viewname == NULL) {
              WARN(key,"ViewName not defined\n");
            }
            else {
              int viewidx = GetIndexByPath(cfg,"View",viewname);
              if( viewidx < 0 ) {
                WARN(key,"View %s not found\n",viewname);
              }
              else
                rc->Key[k].OpData1 = viewidx;
            }
          } break;
          case Op_PTZ_Down:
          case Op_PTZ_Up:
          case Op_PTZ_Left:
          case Op_PTZ_Right:
          case Op_PTZ_DownRight:
          case Op_PTZ_DownLeft:
          case Op_PTZ_UpRight:
          case Op_PTZ_UpLeft:
          case Op_PTZ_ZoomIn:
          case Op_PTZ_ZoomOut:
          case Op_PTZ_GotoPreset:
            break;
          default: WARN(key,"Unhandled opcode %i for action %s\n",rc->Key[k].OpCode,action); break;
        }

      }
      // Sort the keys to make searches quicker
      qsort(rc->Key,rc->KeyCount,sizeof(struct _KeyMap),KeyCompare);
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
    // KEY MAPS
    config_setting_t *keymaps = config_lookup(&cfg,"KeyMaps");
    LoadKeyMaps(plexer,&cfg,keymaps);
//    config_write_file(&cfg, "config.new");
    config_destroy(&cfg);
    return plexer;
}
