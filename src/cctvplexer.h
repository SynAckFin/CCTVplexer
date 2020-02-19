#ifndef _CCTV_PLEXER_INCLUDED_
#define _CCTV_PLEXER_INCLUDED_

typedef struct _Camera     *Camera;
typedef struct _Stream     *Stream;
typedef struct _CameraView *CameraView;
typedef struct _View       *View;
typedef struct _Plexer     *Plexer;

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
    int32_t   Background;
};
struct _Plexer {
    int32_t CameraCount;
    Camera  Camera;
    int32_t ViewCount;
    View    View;
    int32_t Background;
};

Plexer LoadConfig(char *);

#endif
