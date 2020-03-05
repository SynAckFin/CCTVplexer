#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <lirc_client.h>
#include <libcec/cecc.h>
#include <libcec/ceccloader.h>

struct CallbackData {
    int      LircdHandle;          // Its really a file descriptor
    int      LastKeyPress;
    uint64_t LastKeyTime;          // In milliseconds
    int      RepeatCount;
};

// Maximum time (milliseconds) between key presses to consider it a repeat
#define REPEAT_TIME   200
// Release Suffix
#define RELEASE_SUFFIX  "_EVUP"
// The above defines should be configurable

// Used for CEC message logging
static char *LogPrefix[] = {"ERROR","WARNING","NOTICE","TRAFFIC","DEBUG"};
#define MAX_LOG_PREFIX    (sizeof(LogPrefix)/sizeof(char *) - 1)
static int LogLevel = CEC_LOG_ERROR|CEC_LOG_WARNING;
// Needed by libCEC
static libcec_interface_t   CEC_Interface;
static libcec_configuration CEC_Config;
static ICECCallbacks        CEC_Callbacks;
static char                 CEC_Port[50] = { 0 };
// These are the mappings from the TV remote keys
static char *KeyMaps[256] = {
    [CEC_USER_CONTROL_CODE_SELECT]                      = "SELECT",
    [CEC_USER_CONTROL_CODE_UP]                          = "UP",
    [CEC_USER_CONTROL_CODE_DOWN]                        = "DOWN",
    [CEC_USER_CONTROL_CODE_LEFT]                        = "LEFT",
    [CEC_USER_CONTROL_CODE_RIGHT]                       = "RIGHT",
    [CEC_USER_CONTROL_CODE_RIGHT_UP]                    = "RIGHT_UP",
    [CEC_USER_CONTROL_CODE_RIGHT_DOWN]                  = "RIGHT_DOWN",
    [CEC_USER_CONTROL_CODE_LEFT_UP]                     = "LEFT_UP",
    [CEC_USER_CONTROL_CODE_LEFT_DOWN]                   = "LEFT_DOWN",
    [CEC_USER_CONTROL_CODE_ROOT_MENU]                   = "ROOT_MENU",
    [CEC_USER_CONTROL_CODE_SETUP_MENU]                  = "SETUP_MENU",
    [CEC_USER_CONTROL_CODE_CONTENTS_MENU]               = "CONTENTS_MENU",
    [CEC_USER_CONTROL_CODE_FAVORITE_MENU]               = "FAVORITE_MENU",
    [CEC_USER_CONTROL_CODE_EXIT]                        = "EXIT",
    [CEC_USER_CONTROL_CODE_TOP_MENU]                    = "TOP_MENU",
    [CEC_USER_CONTROL_CODE_DVD_MENU]                    = "DVD_MENU",
    [CEC_USER_CONTROL_CODE_NUMBER_ENTRY_MODE]           = "NUMBER_ENTRY_MODE",
    [CEC_USER_CONTROL_CODE_NUMBER11]                    = "KEY_11",
    [CEC_USER_CONTROL_CODE_NUMBER12]                    = "KEY_12",
    [CEC_USER_CONTROL_CODE_NUMBER0]                     = "KEY_0",
    [CEC_USER_CONTROL_CODE_NUMBER1]                     = "KEY_1",
    [CEC_USER_CONTROL_CODE_NUMBER2]                     = "KEY_2",
    [CEC_USER_CONTROL_CODE_NUMBER3]                     = "KEY_3",
    [CEC_USER_CONTROL_CODE_NUMBER4]                     = "KEY_4",
    [CEC_USER_CONTROL_CODE_NUMBER5]                     = "KEY_5",
    [CEC_USER_CONTROL_CODE_NUMBER6]                     = "KEY_6",
    [CEC_USER_CONTROL_CODE_NUMBER7]                     = "KEY_7",
    [CEC_USER_CONTROL_CODE_NUMBER8]                     = "KEY_8",
    [CEC_USER_CONTROL_CODE_NUMBER9]                     = "KEY_9",
    [CEC_USER_CONTROL_CODE_DOT]                         = "DOT",
    [CEC_USER_CONTROL_CODE_ENTER]                       = "ENTER",
    [CEC_USER_CONTROL_CODE_CLEAR]                       = "CLEAR",
    [CEC_USER_CONTROL_CODE_NEXT_FAVORITE]               = "NEXT_FAVORITE",
    [CEC_USER_CONTROL_CODE_CHANNEL_UP]                  = "CHANNEL_UP",
    [CEC_USER_CONTROL_CODE_CHANNEL_DOWN]                = "CHANNEL_DOWN",
    [CEC_USER_CONTROL_CODE_PREVIOUS_CHANNEL]            = "PREVIOUS_CHANNEL",
    [CEC_USER_CONTROL_CODE_SOUND_SELECT]                = "SOUND_SELECT",
    [CEC_USER_CONTROL_CODE_INPUT_SELECT]                = "INPUT_SELECT",
    [CEC_USER_CONTROL_CODE_DISPLAY_INFORMATION]         = "DISPLAY_INFORMATION",
    [CEC_USER_CONTROL_CODE_HELP]                        = "HELP",
    [CEC_USER_CONTROL_CODE_PAGE_UP]                     = "PAGE_UP",
    [CEC_USER_CONTROL_CODE_PAGE_DOWN]                   = "PAGE_DOWN",
    [CEC_USER_CONTROL_CODE_POWER]                       = "POWER",
    [CEC_USER_CONTROL_CODE_VOLUME_UP]                   = "VOLUME_UP",
    [CEC_USER_CONTROL_CODE_VOLUME_DOWN]                 = "VOLUME_DOWN",
    [CEC_USER_CONTROL_CODE_MUTE]                        = "MUTE",
    [CEC_USER_CONTROL_CODE_PLAY]                        = "PLAY",
    [CEC_USER_CONTROL_CODE_STOP]                        = "STOP",
    [CEC_USER_CONTROL_CODE_PAUSE]                       = "PAUSE",
    [CEC_USER_CONTROL_CODE_RECORD]                      = "RECORD",
    [CEC_USER_CONTROL_CODE_REWIND]                      = "REWIND",
    [CEC_USER_CONTROL_CODE_FAST_FORWARD]                = "FAST_FORWARD",
    [CEC_USER_CONTROL_CODE_EJECT]                       = "EJECT",
    [CEC_USER_CONTROL_CODE_FORWARD]                     = "FORWARD",
    [CEC_USER_CONTROL_CODE_BACKWARD]                    = "BACKWARD",
    [CEC_USER_CONTROL_CODE_STOP_RECORD]                 = "STOP_RECORD",
    [CEC_USER_CONTROL_CODE_PAUSE_RECORD]                = "PAUSE_RECORD",
    [CEC_USER_CONTROL_CODE_ANGLE]                       = "ANGLE",
    [CEC_USER_CONTROL_CODE_SUB_PICTURE]                 = "SUB_PICTURE",
    [CEC_USER_CONTROL_CODE_VIDEO_ON_DEMAND]             = "VIDEO_ON_DEMAND",
    [CEC_USER_CONTROL_CODE_ELECTRONIC_PROGRAM_GUIDE]    = "EEPG",
    [CEC_USER_CONTROL_CODE_TIMER_PROGRAMMING]           = "TIMER_PROGRAMMING",
    [CEC_USER_CONTROL_CODE_INITIAL_CONFIGURATION]       = "INITIAL_CONFIGURATION",
    [CEC_USER_CONTROL_CODE_SELECT_BROADCAST_TYPE]       = "SELECT_BROADCAST_TYPE",
    [CEC_USER_CONTROL_CODE_SELECT_SOUND_PRESENTATION]   = "SELECT_SOUND_PRESENTATION",
    [CEC_USER_CONTROL_CODE_PLAY_FUNCTION]               = "PLAY_FUNCTION",
    [CEC_USER_CONTROL_CODE_PAUSE_PLAY_FUNCTION]         = "PAUSE_PLAY_FUNCTION",
    [CEC_USER_CONTROL_CODE_RECORD_FUNCTION]             = "RECORD_FUNCTION",
    [CEC_USER_CONTROL_CODE_PAUSE_RECORD_FUNCTION]       = "PAUSE_RECORD_FUNCTION",
    [CEC_USER_CONTROL_CODE_STOP_FUNCTION]               = "STOP_FUNCTION",
    [CEC_USER_CONTROL_CODE_MUTE_FUNCTION]               = "MUTE_FUNCTION",
    [CEC_USER_CONTROL_CODE_RESTORE_VOLUME_FUNCTION]     = "RESTORE_VOLUME_FUNCTION",
    [CEC_USER_CONTROL_CODE_TUNE_FUNCTION]               = "TUNE_FUNCTION",
    [CEC_USER_CONTROL_CODE_SELECT_MEDIA_FUNCTION]       = "SELECT_MEDIA_FUNCTION",
    [CEC_USER_CONTROL_CODE_SELECT_AV_INPUT_FUNCTION]    = "SELECT_AV_INPUT_FUNCTION",
    [CEC_USER_CONTROL_CODE_SELECT_AUDIO_INPUT_FUNCTION] = "SELECT_AUDIO_INPUT_FUNCTION",
    [CEC_USER_CONTROL_CODE_POWER_TOGGLE_FUNCTION]       = "POWER_TOGGLE_FUNCTION",
    [CEC_USER_CONTROL_CODE_POWER_OFF_FUNCTION]          = "POWER_OFF_FUNCTION",
    [CEC_USER_CONTROL_CODE_POWER_ON_FUNCTION]           = "POWER_ON_FUNCTION",
    [CEC_USER_CONTROL_CODE_F1_BLUE]                     = "F1_BLUE",
    [CEC_USER_CONTROL_CODE_F2_RED]                      = "F2_RED",
    [CEC_USER_CONTROL_CODE_F3_GREEN]                    = "F3_GREEN",
    [CEC_USER_CONTROL_CODE_F4_YELLOW]                   = "F4_YELLOW",
    [CEC_USER_CONTROL_CODE_F5]                          = "F5",
    [CEC_USER_CONTROL_CODE_DATA]                        = "DATA",
    [CEC_USER_CONTROL_CODE_AN_RETURN]                   = "AN_RETURN",
    [CEC_USER_CONTROL_CODE_AN_CHANNELS_LIST]            = "AN_CHANNELS_LIST",
    [CEC_USER_CONTROL_CODE_MAX]                         = "MAX",
    [CEC_USER_CONTROL_CODE_UNKNOWN]                     = "UNKNOWN"
};
// Capture SIGINT for a clean exit
static void SigHandler(int Signal) {
  printf("signal caught: %d - exiting\n", Signal);
  exit(0);
}
//
// KeyPress - Handle a key being pressed - send it to lirc
//
static void KeyPress(struct CallbackData *Data,int KeyCode) {
    struct timeval tv;
    uint64_t msec;

    // Need to determine if this is a key repeat
    gettimeofday(&tv,NULL);
    msec = tv.tv_sec * 1000 + tv.tv_usec/1000;

    if( KeyCode == Data->LastKeyPress ) {
      int tdiff = msec - Data->LastKeyTime;
      if( tdiff < REPEAT_TIME )
        Data->RepeatCount++;
      else
        Data->RepeatCount = 0;
    }
    else {
      Data->RepeatCount = 0;
    }
    Data->LastKeyPress = KeyCode;
    Data->LastKeyTime  = msec;

    char *keysym = KeyMaps[KeyCode] ? KeyMaps[KeyCode] : "UNMAPPED";
    int lircfd = Data->LircdHandle;
    if(lircfd >= 0) {
      if(lirc_simulate(Data->LircdHandle,"CECRemote",keysym,KeyCode,Data->RepeatCount)) {
        printf("Fail\n");
      }
    }
}
//
// KeyRelease - Handle a key being released - send it to lirc
//
static void KeyRelease(struct CallbackData *Data,int KeyCode) {
    char keybuf[BUFSIZ];
    Data->LastKeyTime = 0;
    Data->RepeatCount = 0;

    char *keysym = KeyMaps[KeyCode] ? KeyMaps[KeyCode] : "UNMAPPED";
    snprintf(keybuf,BUFSIZ,"%s%s",keysym,RELEASE_SUFFIX);
    int lircfd = Data->LircdHandle;
    if(lircfd >= 0) {
      if(lirc_simulate(Data->LircdHandle,"CECRemote",keybuf,KeyCode,0)) {
        printf("Fail\n");
      }
    }
}
// Log messages received from the CEC library
static void CB_LogMessage(void *Data, const cec_log_message *Message) {
    const char *level=NULL;

    if( (LogLevel & Message->level) == 0 )
      return;
    // The incoming message has a bit field log level
    // and the assumption is that only one bit is set
    // but that is only an assumption so use the
    // lowest bit that is set for the level string
    for(int i=0,bit=1;i <= MAX_LOG_PREFIX; i++,bit <<= 1) {
      if(Message->level & bit) {
        level = LogPrefix[i];
      }
    }
    if(level == NULL)
      level = "UNKNOWN";
  
    printf("%s[%lld]\t%s\n",level, Message->time, Message->message);
}
// Called when a remote button that we should handle is pressed or released.
// Testing has revealed that this doesn't always get called for a key release
// and that will be needed for PTZ control so you know when to stop pan/tilt
// or zooming.
// Fortunately, the CB_Command callback always gets called on a key release so
// that is where key presses and releases are atually handled and this is only
// used for debugging and testing purposes.
static void CB_KeyPress(void* Data, const cec_keypress *Key) {
    if(0) {
      printf("Key Press %02x (%i)",Key->keycode,Key->duration);
      if(Key->keycode >= 32 && Key->keycode <= 126)
        printf("(%c)",Key->keycode);
      printf("\n");
    }
}
static void CB_Command(void* Data, const cec_command *Command) {
    struct CallbackData *cbd = Data;

    switch(Command->opcode) {
      case CEC_OPCODE_VENDOR_REMOTE_BUTTON_DOWN:
      case CEC_OPCODE_USER_CONTROL_PRESSED:
        KeyPress(cbd,Command->parameters.data[0]);
        break;
      case CEC_OPCODE_VENDOR_REMOTE_BUTTON_UP:
      case CEC_OPCODE_USER_CONTROL_RELEASE:
        KeyRelease(cbd,Command->parameters.data[0]);
        break;
      default: break;
    }
    int i;
    char *s;
    switch(Command->opcode) {
      case CEC_OPCODE_ACTIVE_SOURCE:                 s = "ACTIVE SOURCE";                 break;
      case CEC_OPCODE_IMAGE_VIEW_ON:                 s = "IMAGE VIEW ON";                 break;
      case CEC_OPCODE_TEXT_VIEW_ON:                  s = "TEXT VIEW ON";                  break;
      case CEC_OPCODE_INACTIVE_SOURCE:               s = "INACTIVE SOURCE";               break;
      case CEC_OPCODE_REQUEST_ACTIVE_SOURCE:         s = "REQUEST ACTIVE SOURCE";         break;
      case CEC_OPCODE_ROUTING_CHANGE:                s = "ROUTING CHANGE";                break;
      case CEC_OPCODE_ROUTING_INFORMATION:           s = "ROUTING INFORMATION";           break;
      case CEC_OPCODE_SET_STREAM_PATH:               s = "SET STREAM PATH";               break;
      case CEC_OPCODE_STANDBY:                       s = "STANDBY";                       break;
      case CEC_OPCODE_RECORD_OFF:                    s = "RECORD OFF";                    break;
      case CEC_OPCODE_RECORD_ON:                     s = "RECORD ON";                     break;
      case CEC_OPCODE_RECORD_STATUS:                 s = "RECORD STATUS";                 break;
      case CEC_OPCODE_RECORD_TV_SCREEN:              s = "RECORD TV SCREEN";              break;
      case CEC_OPCODE_CLEAR_ANALOGUE_TIMER:          s = "CLEAR ANALOGUE TIMER";          break;
      case CEC_OPCODE_CLEAR_DIGITAL_TIMER:           s = "CLEAR DIGITAL TIMER";           break;
      case CEC_OPCODE_CLEAR_EXTERNAL_TIMER:          s = "CLEAR EXTERNAL TIMER";          break;
      case CEC_OPCODE_SET_ANALOGUE_TIMER:            s = "SET ANALOGUE TIMER";            break;
      case CEC_OPCODE_SET_DIGITAL_TIMER:             s = "SET DIGITAL TIMER";             break;
      case CEC_OPCODE_SET_EXTERNAL_TIMER:            s = "SET EXTERNAL TIMER";            break;
      case CEC_OPCODE_SET_TIMER_PROGRAM_TITLE:       s = "SET TIMER PROGRAM TITLE";       break;
      case CEC_OPCODE_TIMER_CLEARED_STATUS:          s = "TIMER CLEARED STATUS";          break;
      case CEC_OPCODE_TIMER_STATUS:                  s = "TIMER STATUS";                  break;
      case CEC_OPCODE_CEC_VERSION:                   s = "CEC VERSION";                   break;
      case CEC_OPCODE_GET_CEC_VERSION:               s = "GET CEC VERSION";               break;
      case CEC_OPCODE_GIVE_PHYSICAL_ADDRESS:         s = "GIVE PHYSICAL ADDRESS";         break;
      case CEC_OPCODE_GET_MENU_LANGUAGE:             s = "GET MENU LANGUAGE";             break;
      case CEC_OPCODE_REPORT_PHYSICAL_ADDRESS:       s = "REPORT PHYSICAL ADDRESS";       break;
      case CEC_OPCODE_SET_MENU_LANGUAGE:             s = "SET MENU LANGUAGE";             break;
      case CEC_OPCODE_DECK_CONTROL:                  s = "DECK CONTROL";                  break;
      case CEC_OPCODE_DECK_STATUS:                   s = "DECK STATUS";                   break;
      case CEC_OPCODE_GIVE_DECK_STATUS:              s = "GIVE DECK STATUS";              break;
      case CEC_OPCODE_PLAY:                          s = "PLAY";                          break;
      case CEC_OPCODE_GIVE_TUNER_DEVICE_STATUS:      s = "GIVE TUNER DEVICE STATUS";      break;
      case CEC_OPCODE_SELECT_ANALOGUE_SERVICE:       s = "SELECT ANALOGUE SERVICE";       break;
      case CEC_OPCODE_SELECT_DIGITAL_SERVICE:        s = "SELECT DIGITAL SERVICE";        break;
      case CEC_OPCODE_TUNER_DEVICE_STATUS:           s = "TUNER DEVICE STATUS";           break;
      case CEC_OPCODE_TUNER_STEP_DECREMENT:          s = "TUNER STEP DECREMENT";          break;
      case CEC_OPCODE_TUNER_STEP_INCREMENT:          s = "TUNER STEP INCREMENT";          break;
      case CEC_OPCODE_DEVICE_VENDOR_ID:              s = "DEVICE VENDOR ID";              break;
      case CEC_OPCODE_GIVE_DEVICE_VENDOR_ID:         s = "GIVE DEVICE VENDOR ID";         break;
      case CEC_OPCODE_VENDOR_COMMAND:                s = "VENDOR COMMAND";                break;
      case CEC_OPCODE_VENDOR_COMMAND_WITH_ID:        s = "VENDOR COMMAND WITH ID";        break;
      case CEC_OPCODE_VENDOR_REMOTE_BUTTON_DOWN:     s = "VENDOR REMOTE BUTTON DOWN";     break;
      case CEC_OPCODE_VENDOR_REMOTE_BUTTON_UP:       s = "VENDOR REMOTE BUTTON UP";       break;
      case CEC_OPCODE_SET_OSD_STRING:                s = "SET OSD STRING";                break;
      case CEC_OPCODE_GIVE_OSD_NAME:                 s = "GIVE OSD NAME";                 break;
      case CEC_OPCODE_SET_OSD_NAME:                  s = "SET OSD NAME";                  break;
      case CEC_OPCODE_MENU_REQUEST:                  s = "MENU REQUEST";                  break;
      case CEC_OPCODE_MENU_STATUS:                   s = "MENU STATUS";                   break;
      case CEC_OPCODE_USER_CONTROL_PRESSED:          s = "USER CONTROL PRESSED";          break;
      case CEC_OPCODE_USER_CONTROL_RELEASE:          s = "USER CONTROL RELEASE";          break;
      case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS:      s = "GIVE DEVICE POWER STATUS";      break;
      case CEC_OPCODE_REPORT_POWER_STATUS:           s = "REPORT POWER STATUS";           break;
      case CEC_OPCODE_FEATURE_ABORT:                 s = "FEATURE ABORT";                 break;
      case CEC_OPCODE_ABORT:                         s = "ABORT";                         break;
      case CEC_OPCODE_GIVE_AUDIO_STATUS:             s = "GIVE AUDIO STATUS";             break;
      case CEC_OPCODE_GIVE_SYSTEM_AUDIO_MODE_STATUS: s = "GIVE SYSTEM AUDIO MODE STATUS"; break;
      case CEC_OPCODE_REPORT_AUDIO_STATUS:           s = "REPORT AUDIO STATUS";           break;
      case CEC_OPCODE_SET_SYSTEM_AUDIO_MODE:         s = "SET SYSTEM AUDIO MODE";         break;
      case CEC_OPCODE_SYSTEM_AUDIO_MODE_REQUEST:     s = "SYSTEM AUDIO MODE REQUEST";     break;
      case CEC_OPCODE_SYSTEM_AUDIO_MODE_STATUS:      s = "SYSTEM AUDIO MODE STATUS";      break;
      case CEC_OPCODE_SET_AUDIO_RATE:                s = "SET AUDIO RATE";                break;
      case CEC_OPCODE_START_ARC:                     s = "START ARC";                     break;
      case CEC_OPCODE_REPORT_ARC_STARTED:            s = "REPORT ARC STARTED";            break;
      case CEC_OPCODE_REPORT_ARC_ENDED:              s = "REPORT ARC ENDED";              break;
      case CEC_OPCODE_REQUEST_ARC_START:             s = "REQUEST ARC START";             break;
      case CEC_OPCODE_REQUEST_ARC_END:               s = "REQUEST ARC END";               break;
      case CEC_OPCODE_END_ARC:                       s = "END ARC";                       break;
      case CEC_OPCODE_CDC:                           s = "CDC";                           break;
      case CEC_OPCODE_NONE:                          s = "NONE";                          break;
//      default:                                       s = "UNKNOWN OPCODE";                break;
    }
    if(0) {
      printf("Command: %s",s);
      for(i=0;i<Command->parameters.size;i++) {
        printf(" %02x",Command->parameters.data[i]);
      }
      printf("\n");
    }
    return;
}
static void CB_ConfChanged(void* lib, const libcec_configuration  *conf) {
    printf("CONF CHANGED\n");
}
static void CB_Alert(void* lib, const libcec_alert alert,const libcec_parameter param) {
    printf("CEC ALERT\n");
}
static int CB_StateChange(void* lib, const cec_menu_state state) {
    printf("MENU STATE CHANGED\n");
    return 1;
}
static void CB_SourceActivated(void* lib, const cec_logical_address addr,uint8_t active){
    printf("SOURCE ACTIVATED %i\n",active);
}
// Destroy the interface
static void CEC_Destroy() {
    printf("CEC CLEAN UP\n");
    libcecc_destroy(&CEC_Interface);
}
//
// Initialise the CEC Interface
//   return 0 on success
//
int CECinit(struct CallbackData *CBD) {
    // Initialise config structure
    libcecc_reset_configuration(&CEC_Config);
    // Set up callbacks
    CEC_Callbacks.logMessage           = CB_LogMessage;
    CEC_Callbacks.keyPress             = CB_KeyPress;
    CEC_Callbacks.commandReceived      = CB_Command;
    CEC_Callbacks.configurationChanged = CB_ConfChanged;
    CEC_Callbacks.alert                = CB_Alert;
    CEC_Callbacks.menuStateChanged     = CB_StateChange;
    CEC_Callbacks.sourceActivated      = CB_SourceActivated;
    // Set up the configuration
    CEC_Config.clientVersion    = LIBCEC_VERSION_CURRENT;
    CEC_Config.bActivateSource  = 0;
    CEC_Config.callbackParam    = CBD;
    CEC_Config.callbacks        = &CEC_Callbacks;
    snprintf(CEC_Config.strDeviceName, sizeof(CEC_Config.strDeviceName), "CCTVTEST");
    // Set the device types
    // Set it as multiple devices so that more key presses
    // for the remote get forwarded to us.
    CEC_Config.deviceTypes.types[0] = CEC_DEVICE_TYPE_TUNER;
    CEC_Config.deviceTypes.types[1] = CEC_DEVICE_TYPE_RECORDING_DEVICE;
    CEC_Config.deviceTypes.types[2] = CEC_DEVICE_TYPE_PLAYBACK_DEVICE;
    // Initialise
    if (libcecc_initialise(&CEC_Config, &CEC_Interface, NULL) != 1) {
      printf("can't initialise libCEC\n");
      return -1;
    }
    // init video on targets that need this
    CEC_Interface.init_video_standalone(CEC_Interface.connection);
    // Find a port to connect to
    if (CEC_Port[0] == 0) {
      cec_adapter devices[10];
      int8_t numdevices;

      numdevices = CEC_Interface.find_adapters(CEC_Interface.connection, devices, sizeof(devices) / sizeof(devices), NULL);
      if (numdevices <= 0) {
        printf("Unable to find any devices\n");
        libcecc_destroy(&CEC_Interface);
        return -1;
      }
      else {
        for(int i=0;i < numdevices;i++) {
          printf("Found Device: Path:%s Port:%s\n", devices[0].path, devices[0].comm);
        }
        strcpy(CEC_Port,devices[0].comm);
      }
    }
    // Finally, connect to the device
    if (!CEC_Interface.open(CEC_Interface.connection, CEC_Port, 5000)) {
      printf("unable to open the device on port %s\n", CEC_Port);
      libcecc_destroy(&CEC_Interface);
      return -1;
    }
    atexit(CEC_Destroy);
    return 0;
}

int main() {
    struct CallbackData cbd;
    time_t nextlirctry = 0;         // The next time to connect to lirc

    // No output buffering if it isn't a tty
    if(!isatty(fileno(stdout)))
      setbuf(stdout, NULL);
    // Add a signal handler so know when to stop
    if (signal(SIGINT, SigHandler) == SIG_ERR) {
      printf("can't register sighandler\n");
      return -1;
    }
    // Initialise cbd
    memset(&cbd,0,sizeof(cbd));
    cbd.LircdHandle = -1;
    // Initialise the CEC
    if(1 && CECinit(&cbd)) {
      printf("Failed to initialise CEC\n");
      return -1;
    }
    // Loop handling lirc, everything else is handled in callbacks
    while(1) {
      time_t now = time(NULL);
      if(cbd.LircdHandle < 0 && nextlirctry <= now) {
        if( (cbd.LircdHandle = lirc_get_local_socket(NULL,0)) < 0) {
          printf("Failed to initialise lirc: %s\n",strerror(-cbd.LircdHandle));
          nextlirctry = now + 30;
          cbd.LircdHandle = -1;
        }
        else {
          printf("Successfully connected to lircd\n");
        }
      }
      // Use select to monitor the LircdHandle
      fd_set readfds;
      int nfds;
      struct timeval timeout,*tout = NULL;
      FD_ZERO(&readfds);
      if( cbd.LircdHandle >= 0 ) {
        // Connection is OK so just wait for input
        FD_SET(cbd.LircdHandle,&readfds);
        tout = NULL; // Already set to this but....
        nfds = cbd.LircdHandle+1;
      }
      else {
        // Problem with the connection so set timeout to next connect retry
        tout = &timeout;
        timeout.tv_sec = now < nextlirctry ? nextlirctry - now : 1;
        timeout.tv_usec = 0;
        nfds = 0;
      }
      if( select(nfds,&readfds,NULL,NULL,tout) < 0 ) {
        perror("select error");
        continue;
      }
      //
      // 
      if( cbd.LircdHandle >= 0 && FD_ISSET(cbd.LircdHandle,&readfds) ) {
        //
        // This is just a test to see if the lirc connection has gone
        //
        char buffer[1];
        int n;
        n = recv(cbd.LircdHandle,buffer,1,MSG_PEEK);
        if( (n < 0 && errno != EAGAIN) || n == 0 ) {
          if(n == 0)
            printf("Lost LIRC connection\n");
          else
            perror("Error reading LIRC socket\n");
          // close the socket
          int fd = cbd.LircdHandle;
          cbd.LircdHandle = -1;
          close(fd);
        }
        else {
          // There is something to read on the socket
          // so give the lirc system a chance to read it.
          sleep(1);
        }
      }
    }
    return 0;
}