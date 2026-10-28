#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "layout.h"
static int overlap(CamRect a,CamRect b) {
    return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h;
}
int main(void) {
    int ids[CAM_MAX],cols,rows;
    assert(cam_parse_channels("",ids)==0);
    assert(cam_parse_channels("  ",ids)==0);
    assert(cam_parse_channels("1,2",ids)==2 && ids[0]==1 && ids[1]==2);
    assert(cam_parse_channels(" 1, 3 ,16 ",ids)==3 && ids[2]==16);
    assert(cam_parse_channels("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16",ids)==16);
    const char *bad[]={"0","17","1,1","1,01","1,","1,,2",",1","abc","1;2","1,2x","99999999999999999999","-1"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)assert(cam_parse_channels(bad[i],ids)==-1);
    cam_grid_size(2,CAM_SIDE_BY_SIDE,1280,800,0,&cols,&rows);
    assert(cols==2 && rows==1);
    cam_grid_size(2,CAM_STACKED,1280,800,0,&cols,&rows);
    assert(cols==1 && rows==2);
    cam_grid_size(2,CAM_GRID,1280,800,0,&cols,&rows);
    assert(cols==1 && rows==2);
    for(int n=0;n<=CAM_MAX;n++)for(int mode=0;mode<=2;mode++) {
        for(int header=0;header<=24;header+=24) {
            cam_grid_size(n,mode,1281,801,header,&cols,&rows);
            assert(cols>=1 && rows>=1 && cols*rows>=n);
            for(int i=0;i<n;i++) {
                CamRect a=cam_cell(i,cols,rows,1281,801);
                assert(a.w>0 && a.h>0 && a.x>=0 && a.y>=0);
                assert(a.x+a.w<=1281 && a.y+a.h<=801);
                for(int j=0;j<i;j++)assert(!overlap(a,cam_cell(j,cols,rows,1281,801)));
            }
        }
    }
    puts("OK: lista vazia/16 canais, duplicados e entradas invalidas, tres layouts, limites e sobreposicoes para 0 a 16 cameras.");
    return 0;
}
