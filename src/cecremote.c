#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <sysexits.h>
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
    char     *ReleaseSuffix;
    char     *RemoteName;
};

// Maximum time (milliseconds) between key presses to consider it a repeat
#define REPEAT_TIME   200
// The above defines should be configurable
static struct option Options[] = {
  {"osd",         required_argument, 0,  'o' },    // On screen display name
  {"port",        required_argument, 0,  'p' },    // CEC port to use
  {"release",     required_argument, 0,  'r' },    // Suffix indicating key release
  {"remotename",  required_argument, 0,  'n' },    // Name to use for the remote control
  {"loglevel",    required_argument, 0,  'l' },    // loglevel
  {"cecloglevel", required_argument, 0,  'c' },    // CEC loglevel
  {0,         0,                 0,  0 }
};
//
static int Stop = 0;
// Used for CEC message logging
static char *LogPrefix[] = {"ERROR","WARNING","NOTICE","TRAFFIC","DEBUG"};
#define MAX_LOG_PREFIX    (sizeof(LogPrefix)/sizeof(char *) - 1)
static int LogLevel = 0;
// Needed by libCEC
static int CECLogLevel = CEC_LOG_ERROR|CEC_LOG_WARNING;
static libcec_interface_t   CEC_Interface;
static libcec_configuration CEC_Config;
static ICECCallbacks        CEC_Callbacks;
static char                 CEC_Port[50] = { 0 };
// These are the mappings from the TV remote control keys
static char *KeyMaps[256] = {
    [CEC_USER_CONTROL_CODE_SELECT]                      = "KEY_SELECT",
    [CEC_USER_CONTROL_CODE_UP]                          = "KEY_UP",
    [CEC_USER_CONTROL_CODE_DOWN]                        = "KEY_DOWN",
    [CEC_USER_CONTROL_CODE_LEFT]                        = "KEY_LEFT",
    [CEC_USER_CONTROL_CODE_RIGHT]                       = "KEY_RIGHT",
    [CEC_USER_CONTROL_CODE_RIGHT_UP]                    = "KEY_RIGHT_UP",
    [CEC_USER_CONTROL_CODE_RIGHT_DOWN]                  = "KEY_RIGHT_DOWN",
    [CEC_USER_CONTROL_CODE_LEFT_UP]                     = "KEY_LEFT_UP",
    [CEC_USER_CONTROL_CODE_LEFT_DOWN]                   = "KEY_LEFT_DOWN",
    [CEC_USER_CONTROL_CODE_ROOT_MENU]                   = "KEY_ROOT_MENU",
    [CEC_USER_CONTROL_CODE_SETUP_MENU]                  = "KEY_SETUP_MENU",
    [CEC_USER_CONTROL_CODE_CONTENTS_MENU]               = "KEY_CONTENTS_MENU",
    [CEC_USER_CONTROL_CODE_FAVORITE_MENU]               = "KEY_FAVORITE_MENU",
    [CEC_USER_CONTROL_CODE_EXIT]                        = "KEY_EXIT",
    [CEC_USER_CONTROL_CODE_TOP_MENU]                    = "KEY_TOP_MENU",
    [CEC_USER_CONTROL_CODE_DVD_MENU]                    = "KEY_DVD_MENU",
    [CEC_USER_CONTROL_CODE_NUMBER_ENTRY_MODE]           = "KEY_NUMBER_ENTRY_MODE",
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
    [CEC_USER_CONTROL_CODE_DOT]                         = "KEY_DOT",
    [CEC_USER_CONTROL_CODE_ENTER]                       = "KEY_ENTER",
    [CEC_USER_CONTROL_CODE_CLEAR]                       = "KEY_CLEAR",
    [CEC_USER_CONTROL_CODE_NEXT_FAVORITE]               = "KEY_NEXT_FAVORITE",
    [CEC_USER_CONTROL_CODE_CHANNEL_UP]                  = "KEY_CHANNEL_UP",
    [CEC_USER_CONTROL_CODE_CHANNEL_DOWN]                = "KEY_CHANNEL_DOWN",
    [CEC_USER_CONTROL_CODE_PREVIOUS_CHANNEL]            = "KEY_PREVIOUS_CHANNEL",
    [CEC_USER_CONTROL_CODE_SOUND_SELECT]                = "KEY_SOUND_SELECT",
    [CEC_USER_CONTROL_CODE_INPUT_SELECT]                = "KEY_INPUT_SELECT",
    [CEC_USER_CONTROL_CODE_DISPLAY_INFORMATION]         = "KEY_DISPLAY_INFORMATION",
    [CEC_USER_CONTROL_CODE_HELP]                        = "KEY_HELP",
    [CEC_USER_CONTROL_CODE_PAGE_UP]                     = "KEY_PAGE_UP",
    [CEC_USER_CONTROL_CODE_PAGE_DOWN]                   = "KEY_PAGE_DOWN",
    [CEC_USER_CONTROL_CODE_POWER]                       = "KEY_POWER",
    [CEC_USER_CONTROL_CODE_VOLUME_UP]                   = "KEY_VOLUME_UP",
    [CEC_USER_CONTROL_CODE_VOLUME_DOWN]                 = "KEY_VOLUME_DOWN",
    [CEC_USER_CONTROL_CODE_MUTE]                        = "KEY_MUTE",
    [CEC_USER_CONTROL_CODE_PLAY]                        = "KEY_PLAY",
    [CEC_USER_CONTROL_CODE_STOP]                        = "KEY_STOP",
    [CEC_USER_CONTROL_CODE_PAUSE]                       = "KEY_PAUSE",
    [CEC_USER_CONTROL_CODE_RECORD]                      = "KEY_RECORD",
    [CEC_USER_CONTROL_CODE_REWIND]                      = "KEY_REWIND",
    [CEC_USER_CONTROL_CODE_FAST_FORWARD]                = "KEY_FAST_FORWARD",
    [CEC_USER_CONTROL_CODE_EJECT]                       = "KEY_EJECT",
    [CEC_USER_CONTROL_CODE_FORWARD]                     = "KEY_FORWARD",
    [CEC_USER_CONTROL_CODE_BACKWARD]                    = "KEY_BACKWARD",
    [CEC_USER_CONTROL_CODE_STOP_RECORD]                 = "KEY_STOP_RECORD",
    [CEC_USER_CONTROL_CODE_PAUSE_RECORD]                = "KEY_PAUSE_RECORD",
    [CEC_USER_CONTROL_CODE_ANGLE]                       = "KEY_ANGLE",
    [CEC_USER_CONTROL_CODE_SUB_PICTURE]                 = "KEY_SUB_PICTURE",
    [CEC_USER_CONTROL_CODE_VIDEO_ON_DEMAND]             = "KEY_VIDEO_ON_DEMAND",
    [CEC_USER_CONTROL_CODE_ELECTRONIC_PROGRAM_GUIDE]    = "KEY_EEPG",
    [CEC_USER_CONTROL_CODE_TIMER_PROGRAMMING]           = "KEY_TIMER_PROGRAMMING",
    [CEC_USER_CONTROL_CODE_INITIAL_CONFIGURATION]       = "KEY_INITIAL_CONFIGURATION",
    [CEC_USER_CONTROL_CODE_SELECT_BROADCAST_TYPE]       = "KEY_SELECT_BROADCAST_TYPE",
    [CEC_USER_CONTROL_CODE_SELECT_SOUND_PRESENTATION]   = "KEY_SELECT_SOUND_PRESENTATION",
    [CEC_USER_CONTROL_CODE_PLAY_FUNCTION]               = "KEY_PLAY_FUNCTION",
    [CEC_USER_CONTROL_CODE_PAUSE_PLAY_FUNCTION]         = "KEY_PAUSE_PLAY_FUNCTION",
    [CEC_USER_CONTROL_CODE_RECORD_FUNCTION]             = "KEY_RECORD_FUNCTION",
    [CEC_USER_CONTROL_CODE_PAUSE_RECORD_FUNCTION]       = "KEY_PAUSE_RECORD_FUNCTION",
    [CEC_USER_CONTROL_CODE_STOP_FUNCTION]               = "KEY_STOP_FUNCTION",
    [CEC_USER_CONTROL_CODE_MUTE_FUNCTION]               = "KEY_MUTE_FUNCTION",
    [CEC_USER_CONTROL_CODE_RESTORE_VOLUME_FUNCTION]     = "KEY_RESTORE_VOLUME_FUNCTION",
    [CEC_USER_CONTROL_CODE_TUNE_FUNCTION]               = "KEY_TUNE_FUNCTION",
    [CEC_USER_CONTROL_CODE_SELECT_MEDIA_FUNCTION]       = "KEY_SELECT_MEDIA_FUNCTION",
    [CEC_USER_CONTROL_CODE_SELECT_AV_INPUT_FUNCTION]    = "KEY_SELECT_AV_INPUT_FUNCTION",
    [CEC_USER_CONTROL_CODE_SELECT_AUDIO_INPUT_FUNCTION] = "KEY_SELECT_AUDIO_INPUT_FUNCTION",
    [CEC_USER_CONTROL_CODE_POWER_TOGGLE_FUNCTION]       = "KEY_POWER_TOGGLE_FUNCTION",
    [CEC_USER_CONTROL_CODE_POWER_OFF_FUNCTION]          = "KEY_POWER_OFF_FUNCTION",
    [CEC_USER_CONTROL_CODE_POWER_ON_FUNCTION]           = "KEY_POWER_ON_FUNCTION",
    [CEC_USER_CONTROL_CODE_F1_BLUE]                     = "KEY_F1_BLUE",
    [CEC_USER_CONTROL_CODE_F2_RED]                      = "KEY_F2_RED",
    [CEC_USER_CONTROL_CODE_F3_GREEN]                    = "KEY_F3_GREEN",
    [CEC_USER_CONTROL_CODE_F4_YELLOW]                   = "KEY_F4_YELLOW",
    [CEC_USER_CONTROL_CODE_F5]                          = "KEY_F5",
    [CEC_USER_CONTROL_CODE_DATA]                        = "KEY_DATA",
    [CEC_USER_CONTROL_CODE_AN_RETURN]                   = "KEY_AN_RETURN",
    [CEC_USER_CONTROL_CODE_AN_CHANNELS_LIST]            = "KEY_AN_CHANNELS_LIST",
    [CEC_USER_CONTROL_CODE_MAX]                         = "KEY_MAX",
    [CEC_USER_CONTROL_CODE_UNKNOWN]                     = "KEY_UNKNOWN"
};
// Capture SIGINT for a clean exit
static void SigHandler(int Signal) {
  printf("signal caught: %d - exiting\n", Signal);
  Stop = 1;
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
      uint64_t tdiff = msec - Data->LastKeyTime;
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
      if(lirc_simulate(Data->LircdHandle,Data->RemoteName,keysym,KeyCode,Data->RepeatCount)) {
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
    snprintf(keybuf,BUFSIZ,"%s%s",keysym,Data->ReleaseSuffix);
    int lircfd = Data->LircdHandle;
    if(lircfd >= 0) {
      if(lirc_simulate(Data->LircdHandle,Data->RemoteName,keybuf,KeyCode,0)) {
        printf("Fail\n");
      }
    }
}
// Log messages received from the CEC library
static void CB_LogMessage(void *Data, const cec_log_message *Message) {
    const char *level=NULL;

    if( (CECLogLevel & Message->level) == 0 )
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
//
// Initialise the CEC Interface
//   return 0 on success
//
int CECinit(struct CallbackData *CBD,char *Name,char *Port) {
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
    snprintf(CEC_Config.strDeviceName, sizeof(CEC_Config.strDeviceName), Name);
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
    if(Port) {
      strncpy(CEC_Port,Port,sizeof(CEC_Port));
      // make sure it is terminated
      CEC_Port[sizeof(CEC_Port)-1] = 0;
    }
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
    printf("Using port %s\n",CEC_Port);
    // Finally, connect to the device
    if (!CEC_Interface.open(CEC_Interface.connection, CEC_Port, 5000)) {
      printf("unable to open the device on port %s\n", CEC_Port);
      libcecc_destroy(&CEC_Interface);
      return -1;
    }
    return 0;
}
static void usage(char *Name) {
    char *s;
    // Skip any path in Name
    if( (s = strrchr(Name,'/')) == NULL)
      s = Name;
    else
      s++;
    fprintf(stderr,"usage: %s [options]\n",s);
    fprintf(stderr,"\nOptions:\n");
    fprintf(stderr," %-30s%s\n","-o, --osd <osdname>","The string to use for the On screen name");
    fprintf(stderr," %-30s%s\n","-p, --port <port>","The CEC port to connect to");
    fprintf(stderr," %-30s%s\n","-r, --release <string>","LIRC string to append for button release");
    fprintf(stderr," %-30s%s\n","-n, --remotename <name>","LIRC name of the remote");
    fprintf(stderr," %-30s%s\n","-l, --loglevel <level>","Logging level");
    fprintf(stderr," %-30s%s\n","-c, --cecloglevel <bits>","CEC logging level (bits)");
    fprintf(stderr," %-32s%s\n","","CEC_LOG_ERROR   0x01");
    fprintf(stderr," %-32s%s\n","","CEC_LOG_WARNING 0x02");
    fprintf(stderr," %-32s%s\n","","CEC_LOG_NOTICE  0x04");
    fprintf(stderr," %-32s%s\n","","CEC_LOG_TRAFFIC 0x08");
    fprintf(stderr," %-32s%s\n","","CEC_LOG_DEBUG   0x10");
    fprintf(stderr," %-32s%s\n","","CEC_LOG_ALL     0x1F");
    exit(EX_USAGE);
}
int main(int ac, char *av[]) {
    struct CallbackData cbd;
    time_t nextlirctry = 0;               // The next time to connect to lirc
    // options
    char *osd  = "CCTVTEST";              // On screen display name
    char *port = NULL;                    // CEC port
    char *release = LIRC_RELEASE_SUFFIX;
    char *rname = "CECRemote";

    int c;
    int idx = 0;
    while ( (c = getopt_long(ac, av, "o:p:r:n:l:c:",Options,&idx)) >= 0) {
      switch (c) {
        case 0:
          printf("Oops, my mistake; check def for --%s\n",Options[idx].name);
          break;
        case 'o': osd = optarg; break;
        case 'p': port = optarg; break;
        case 'r': release = optarg; break;
        case 'n': rname = optarg; break;
        case 'l': LogLevel = strtol(optarg,NULL,0); break;
        case 'c': CECLogLevel = strtol(optarg,NULL,0); break;
        case '?':
        default:  usage(av[0]); break;
      }
    }
    if (optind < ac) {
      fprintf(stderr,"Unexpected arguments: ");
      while (optind < ac)
        fprintf(stderr,"%s ", av[optind++]);
      fprintf(stderr,"\n");
      usage(av[0]);
    }
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
    cbd.RemoteName = rname;
    cbd.ReleaseSuffix = release;
    // Initialise the CEC
    if(CECinit(&cbd,osd,port)) {
      fprintf(stderr,"Failed to initialise CEC\n");
      return EX_UNAVAILABLE;
    }
    // Loop handling lirc, everything else is handled in libCEC callbacks
    while(Stop == 0) {
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
    libcecc_destroy(&CEC_Interface);
    return EX_OK;
}
