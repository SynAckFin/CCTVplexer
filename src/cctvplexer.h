#ifndef _CCTV_PLEXER_INCLUDED_
#define _CCTV_PLEXER_INCLUDED_


typedef struct _Camera        *Camera;
typedef struct _Stream        *Stream;
typedef struct _CameraView    *CameraView;
typedef struct _View          *View;
typedef struct _Plexer        *Plexer;
typedef struct _KeyMap        *KeyMap;
typedef struct _RemoteControl *RemoteControl;
typedef enum   _OpCode        OpCode;

enum _OpCode {
    Op_None = 0,
    Op_SetView,
    Op_NextView,
    Op_PrevView,
    Op_Quit
};
struct _Camera {
    char    *Name;
    int32_t ControlID;
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
    int32_t    Background;
};
struct _KeyMap {
    char    *Key;
    OpCode  OpCode;
    int32_t OpData1;
    int32_t OpData2;          // Unused
    int32_t OpData3;          // Unused
    int32_t OpData4;          // Unused
};
struct _RemoteControl {
    char    *Name;
    int32_t KeyCount;
    KeyMap  Key;
};
struct _Plexer {
    int32_t       CameraCount;
    Camera        Camera;
    int32_t       ViewCount;
    View          View;
    int32_t       CurrentView;
    int32_t       RemoteControlCount;
    RemoteControl RemoteControl;
    int32_t       Background;
};

Plexer LoadConfig(char *);
#endif
