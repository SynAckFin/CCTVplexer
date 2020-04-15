#ifndef _CCTV_PLEXER_INCLUDED_
#define _CCTV_PLEXER_INCLUDED_


typedef struct _Camera        *Camera;
typedef struct _Stream        *Stream;
typedef struct _CameraView    *CameraView;
typedef struct _View          *View;
typedef struct _Plexer        *Plexer;
typedef struct _KeyMap        *KeyMap;
typedef struct _RemoteControl *RemoteControl;
typedef struct _PTZController *PTZController;
typedef enum   _OpCode        OpCode;
typedef enum   _HttpMethod    HttpMethod;

enum _OpCode {
    Op_None = 0,
    Op_PTZ_None = 0,
    Op_PTZ_Stop,
    Op_PTZ_Up,
    Op_PTZ_UpRight,
    Op_PTZ_Right,
    Op_PTZ_DownRight,
    Op_PTZ_Down,
    Op_PTZ_DownLeft,
    Op_PTZ_Left,
    Op_PTZ_UpLeft,
    Op_PTZ_ZoomIn,
    Op_PTZ_ZoomOut,
    Op_PTZ_FocusNear,
    Op_PTZ_FocusFar,
    Op_PTZ_IrisClose,
    Op_PTZ_IrisOpen,
    Op_PTZ_GotoPreset,
    Op_PTZ_SetPreset,
    Op_PTZ_ClearPreset,
    Op_PTZ_Max,             // This must be the last PTZ Op
    Op_SetView,
    Op_NextView,
    Op_PrevView,
    Op_Quit,
    Op_Max
};
enum _HttpMethod {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE
};
struct _Camera {
    char    *Name;
    int32_t ControlID;
    PTZController PTZ;
    char    **StreamCommand;
    int     StreamPipe[2];
    void    *RenderHandle;
    pid_t   Child;
};
struct _CameraView {
    int32_t Camera;
    double  X,Y,W,H;
    double  Alpha;
    int32_t Layer;
    int32_t FullScreen:1;
    int32_t KeepAspect:1;
    int32_t Visible:1;
};
struct _View {
    CameraView View;
    Camera     Focus;
    int32_t    BackgroundColour;
    char      *BackgroundImage;
};
struct _KeyMap {
    char    *Key;
    int32_t RepeatCount;
    OpCode  OpCode;
    int32_t OpData1;
    int32_t OpData2;
    int32_t OpData3;
    int32_t OpData4;
    int32_t OpData5;
};
struct _RemoteControl {
    char    *Name;
    int32_t KeyCount;
    KeyMap  Key;
};
struct _Plexer {
    int32_t       CameraCount;
    Camera        Camera;
    Camera        Focus;
    int32_t       ViewCount;
    View          View;
    int32_t       CurrentView;
    int32_t       RemoteControlCount;
    RemoteControl RemoteControl;
    int32_t       BackgroundColour;
    char         *BackgroundImage;
};
struct _PTZController {
    char *Name;
    struct {
      char       *URL;
      HttpMethod  Method;
      char       *Content;
      char       *ContentType;
    } Control[Op_PTZ_Max];
};

Plexer LoadConfig(char *);
void MonitorInitialise(void);
#endif
