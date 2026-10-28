#ifndef CAMXP_LAYOUT_H
#define CAMXP_LAYOUT_H
#define CAM_MAX 16
enum { CAM_SIDE_BY_SIDE, CAM_STACKED, CAM_GRID };
typedef struct {int x,y,w,h;} CamRect;

/* Empty means no cameras. Reject malformed lists and duplicate channels. */
static int cam_parse_channels(const char *s,int out[CAM_MAX]) {
    int n=0,channel;
    while(*s==' ' || *s=='\t')s++;
    if(!*s)return 0;
    for(;;) {
        channel=0;
        if(*s<'0' || *s>'9')return -1;
        do {channel=channel*10+(*s++-'0');if(channel>CAM_MAX)return -1;}while(*s>='0' && *s<='9');
        if(channel<1 || n>=CAM_MAX)return -1;
        for(int i=0;i<n;i++)if(out[i]==channel)return -1;
        out[n++]=channel;
        while(*s==' ' || *s=='\t')s++;
        if(!*s)return n;
        if(*s++!=',')return -1;
        while(*s==' ' || *s=='\t')s++;
    }
}
static void cam_grid_size(int count,int mode,int width,int height,int header,int *cols,int *rows) {
    if(count<1){*cols=*rows=1;return;}
    if(mode==CAM_SIDE_BY_SIDE){*cols=count;*rows=1;return;}
    if(mode==CAM_STACKED){*cols=1;*rows=count;return;}
    int best=-1,best_empty=CAM_MAX;
    *cols=1;*rows=count;
    for(int c=1;c<=count;c++) {
        int r=(count+c-1)/c;
        int w=width/c-2,h=height/r-header-2;
        if(w<1)w=1;
        if(h<1)h=1;
        /* Maximize the actual size of each 16:9 image, not panel area. */
        int scale=w*9<h*16?w*9:h*16,empty=c*r-count;
        if(scale>best || (scale==best && empty<best_empty)) {
            best=scale;best_empty=empty;*cols=c;*rows=r;
        }
    }
}
static CamRect cam_cell(int index,int cols,int rows,int width,int height) {
    CamRect r;
    int col=index%cols,row=index/cols;
    r.x=col*width/cols;r.y=row*height/rows;
    r.w=(col+1)*width/cols-r.x;r.h=(row+1)*height/rows-r.y;
    return r;
}
#endif
