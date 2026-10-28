/* SimpleSecCamViewer: native Win32 host for up to 16 cameras using installed 32-bit LibVLC 3.
 * No ActiveX, browser, server, external C runtime or shell execution.
 * Build instructions and limitations are in LEIA-ME.txt.
 */
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "watchdog.h"
#include "layout.h"

typedef void VLC;
typedef void Media;
typedef void Player;
/* Public LibVLC 3 ABI: include/vlc/libvlc_media.h. */
typedef struct {
    int read_bytes; float input_bitrate;
    int demux_read_bytes; float demux_bitrate;
    int demux_corrupted, demux_discontinuity;
    int decoded_video, decoded_audio, displayed_pictures, lost_pictures;
    int played_abuffers, lost_abuffers, sent_packets, sent_bytes;
    float send_bitrate;
} MediaStats;
typedef char MediaStats_size_must_be_60[(sizeof(MediaStats)==60)?1:-1];
#define API(ret, name, args) static ret (__cdecl *name) args
API(VLC *, libvlc_new, (int, const char *const *));
API(void, libvlc_release, (VLC *));
API(Media *, libvlc_media_new_location, (VLC *, const char *));
API(void, libvlc_media_add_option, (Media *, const char *));
API(void, libvlc_media_release, (Media *));
API(_Bool, libvlc_media_get_stats, (Media *, MediaStats *));
API(Player *, libvlc_media_player_new, (VLC *));
API(void, libvlc_media_player_set_media, (Player *, Media *));
API(void, libvlc_media_player_set_hwnd, (Player *, void *));
API(int, libvlc_media_player_play, (Player *));
API(void, libvlc_media_player_stop, (Player *));
API(void, libvlc_media_player_release, (Player *));
API(int, libvlc_media_player_get_state, (Player *));
API(unsigned, libvlc_media_player_has_vout, (Player *));
API(void, libvlc_video_set_mouse_input, (Player *, unsigned));
API(void, libvlc_video_set_key_input, (Player *, unsigned));

static HWND mainwin, panel[CAM_MAX], label[CAM_MAX], footer, empty_notice;
static HMENU main_menu;
static HACCEL accelerators;
static int fullscreen;
static int overlay_shown[CAM_MAX];
static int enabled[CAM_MAX], layout_mode=CAM_SIDE_BY_SIDE;
static int needs_migration;
static LONG normal_style;
static WINDOWPLACEMENT normal_placement;
static Media *media_ref[CAM_MAX];
static CamWatchdog watchdog[CAM_MAX];
static unsigned attempts[CAM_MAX], after_stop[CAM_MAX];
static DWORD retry_at[CAM_MAX], last_connect;
static int waiting[CAM_MAX], made_connection, stats_warned[CAM_MAX];
static HANDLE stopping[CAM_MAX];
typedef struct {Player *player;Media *media;} StopJob;
static StopJob stopjob[CAM_MAX];
static VLC *vlc;
static Player *player[CAM_MAX];
static HMODULE core_dll, vlc_dll;
static HANDLE log_file = INVALID_HANDLE_VALUE;
static DWORD startup_error;
static char base[MAX_PATH], vlcdir[MAX_PATH], nvr[128], user[128], password[256];
static char channels[CAM_MAX][16], batch[32768], pathbuf[1024], url[2048], text[512];
static char ini[MAX_PATH], renderer[32], old_status[CAM_MAX][128];
static int closing;
static const char *const states[] = {
    "Aguardando", "Conectando", "Carregando", "Reproduzindo",
    "Pausada", "Parada", "Encerrada", "Erro de conexao/video"
};
static void layout(void);
static void rebuild_menus(void);

static void copystr(char *dst, const char *src, unsigned cap) {
    unsigned i = 0;
    if (!cap) return;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static void append(char *dst, const char *src, unsigned cap) {
    unsigned n = (unsigned) lstrlenA(dst);
    if (n < cap) copystr(dst+n, src, cap-n);
}
static int equal(const char *a, const char *b) { return lstrcmpA(a,b)==0; }
static void clearbytes(void *ptr, unsigned size) {
    volatile unsigned char *p=(volatile unsigned char *)ptr;
    while(size--)*p++=0;
}
static void logline(const char *s) {
    DWORD written;
    SYSTEMTIME now;
    char stamp[64];
    if (log_file == INVALID_HANDLE_VALUE) return;
    GetLocalTime(&now);
    wsprintfA(stamp, "%02u:%02u:%02u ", now.wHour, now.wMinute, now.wSecond);
    WriteFile(log_file, stamp, lstrlenA(stamp), &written, NULL);
    WriteFile(log_file, s, lstrlenA(s), &written, NULL);
    WriteFile(log_file, "\r\n", 2, &written, NULL);
    FlushFileBuffers(log_file);
}
static void makepath(char *out, unsigned cap, const char *dir, const char *name) {
    copystr(out,dir,cap); append(out,"\\",cap); append(out,name,cap);
}
/* Read SET assignments as data only. Never execute the legacy BAT. */
static void batch_value(const char *key, char *out, unsigned cap) {
    char *p = batch, *start, *end, *value;
    unsigned k = (unsigned) lstrlenA(key), len;
    while (*p) {
        start = p;
        while (*p && *p!='\r' && *p!='\n') p++;
        end = p;
        while (*p=='\r' || *p=='\n') p++;
        while (start < end && (*start==' ' || *start=='\t')) start++;
        if (end-start < 5 || (start[0]!='s' && start[0]!='S') ||
            (start[1]!='e' && start[1]!='E') || (start[2]!='t' && start[2]!='T') || start[3]!=' ') continue;
        start += 4;
        while (start < end && *start==' ') start++;
        if (start < end && *start=='"') { start++; if (end>start && end[-1]=='"') end--; }
        if ((unsigned)(end-start) <= k || start[k]!='=') continue;
        if (CompareStringA(LOCALE_INVARIANT,NORM_IGNORECASE,start,k,key,k)!=CSTR_EQUAL) continue;
        value = start+k+1;
        len = (unsigned)(end-value);
        if (len>=cap) len=cap-1;
        for (unsigned i=0;i<len;i++) out[i]=value[i];
        out[len]=0;
    }
}
static int valid_channel(const char *s) {
    unsigned n=0;
    if (!*s) return 0;
    while (*s) { if (*s<'0'||*s>'9') return 0; n=n*10+(*s++-'0'); if(n>CAM_MAX)return 0; }
    return n>0;
}
/* Stage the INI first, preserve other sections, then rename the previous file
 * to a unique backup before replacement. No credentials are stored here. */
static int save_settings(void) {
    char list[128];
    list[0]=0;
    for(int i=0;i<CAM_MAX;i++)if(enabled[i]) {
        if(list[0])append(list,",",sizeof list);
        append(list,channels[i],sizeof list);
    }
    const char *mode=layout_mode==CAM_STACKED?"empilhado":layout_mode==CAM_GRID?"mosaico":"lado_a_lado";
    static char temporary[MAX_PATH], backup[MAX_PATH], suffix[96];
    HANDLE file;
    SYSTEMTIME now;
    int exists=GetFileAttributesA(ini)!=INVALID_FILE_ATTRIBUTES;
    GetLocalTime(&now);
    wsprintfA(suffix,"camxp-%04u%02u%02u-%02u%02u%02u-%lu.tmp",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetTickCount());
    makepath(temporary,sizeof temporary,base,suffix);
    file=CreateFileA(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return 0;
    CloseHandle(file);
    if(exists && !CopyFileA(ini,temporary,FALSE)) {DeleteFileA(temporary);return 0;}
    if(!WritePrivateProfileStringA("cameras","canais",list,temporary) ||
       !WritePrivateProfileStringA("interface","layout",mode,temporary)) {
        DeleteFileA(temporary);return 0;
    }
    WritePrivateProfileStringA(NULL,NULL,NULL,temporary);
    if(exists) {
        copystr(backup,ini,sizeof backup);
        wsprintfA(suffix,".%04u%02u%02u-%02u%02u%02u-%lu.bak",
            now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetTickCount());
        append(backup,suffix,sizeof backup);
        if(!MoveFileA(ini,backup)) {DeleteFileA(temporary);return 0;}
    }
    if(!MoveFileA(temporary,ini)) {
        if(exists && !MoveFileA(backup,ini)) {
            logline("Falha ao restaurar camxp.ini. A copia anterior esta em camxp.ini.*.bak.");
        }
        DeleteFileA(temporary);return 0;
    }
    /* Discard the profile cache for the replaced pathname. */
    WritePrivateProfileStringA(NULL,NULL,NULL,ini);
    return 1;
}
static int loadconfig(void) {
    HANDLE file;
    DWORD size=0, attr;
    copystr(nvr,"",sizeof nvr);
    copystr(user,"admin",sizeof user);

    copystr(vlcdir,"C:\\Arquivos de programas\\VideoLAN\\VLC\\vlc.exe",sizeof vlcdir);
    makepath(pathbuf,sizeof pathbuf,base,"iniciar.bat");
    file=CreateFileA(pathbuf,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(file!=INVALID_HANDLE_VALUE) {
        if (GetFileSize(file,NULL)>=sizeof batch) { CloseHandle(file); return 0; }
        if(!ReadFile(file,batch,sizeof batch-1,&size,NULL))size=0;
        CloseHandle(file); batch[size]=0;
        batch_value("NVR",nvr,sizeof nvr); batch_value("USUARIO",user,sizeof user);
        batch_value("SENHA",password,sizeof password); batch_value("VLC",vlcdir,sizeof vlcdir);
    } else { return 0; }
    if (!nvr[0] || !user[0]) return 0;
    /* Optional settings contain no credentials. Default follows VLC preferences. */
    makepath(ini,sizeof ini,base,"camxp.ini");
    char list[128],mode[32];int ids[CAM_MAX],count;
    DWORD used=GetPrivateProfileStringA("cameras","canais","~",list,sizeof list,ini);
    if(used>=sizeof list-1)return 0;
    needs_migration=equal(list,"~");
    if(needs_migration) {
        GetPrivateProfileStringA("cameras","esquerda","1",channels[0],sizeof channels[0],ini);
        GetPrivateProfileStringA("cameras","direita","2",channels[1],sizeof channels[1],ini);
        if(!valid_channel(channels[0]) || !valid_channel(channels[1]))return 0;
        copystr(list,channels[0],sizeof list);
        if(!equal(channels[0],channels[1])){append(list,",",sizeof list);append(list,channels[1],sizeof list);}
    }
    count=cam_parse_channels(list,ids);if(count<0)return 0;
    for(int i=0;i<count;i++){enabled[i]=1;wsprintfA(channels[i],"%d",ids[i]);}
    GetPrivateProfileStringA("interface","layout","lado_a_lado",mode,sizeof mode,ini);
    if(equal(mode,"empilhado"))layout_mode=CAM_STACKED;
    else if(equal(mode,"mosaico"))layout_mode=CAM_GRID;
    else if(!equal(mode,"lado_a_lado"))return 0;
    GetPrivateProfileStringA("video","saida","auto",renderer,sizeof renderer,ini);
    if(!equal(renderer,"auto") && !equal(renderer,"direct3d9") && !equal(renderer,"wingdi"))return 0;
    unsigned len=(unsigned)lstrlenA(vlcdir);
    while(len && vlcdir[len-1]!='\\') len--;
    if(!len)return 0;
    vlcdir[len-1]=0;
    makepath(pathbuf,sizeof pathbuf,vlcdir,"libvlc.dll");
    attr=GetFileAttributesA(pathbuf);
    return attr!=INVALID_FILE_ATTRIBUTES && !(attr&FILE_ATTRIBUTE_DIRECTORY);
}
#define LOAD(name) do { *(FARPROC *)&name=GetProcAddress(vlc_dll,#name); if(!name) { startup_error=GetLastError(); logline("Funcao ausente: " #name); return 0; } } while(0)
static int initvlc(void) {
    const char *args[14];
    int argc=0;
    static char config_arg[1024], render_arg[64], appdata[MAX_PATH], profile[MAX_PATH];
    SetDllDirectoryA(vlcdir);
    makepath(pathbuf,sizeof pathbuf,vlcdir,"plugins");
    SetEnvironmentVariableA("VLC_PLUGIN_PATH",pathbuf);
    makepath(pathbuf,sizeof pathbuf,vlcdir,"libvlccore.dll");
    logline("Carregando libvlccore.dll.");
    core_dll=LoadLibraryA(pathbuf);
    if(!core_dll) {
        startup_error=GetLastError();
        wsprintfA(text,"Falha libvlccore.dll: Windows %lu.",startup_error);logline(text);return 0;
    }
    makepath(pathbuf,sizeof pathbuf,vlcdir,"libvlc.dll");
    logline("Carregando libvlc.dll.");
    vlc_dll=LoadLibraryA(pathbuf);
    if(!vlc_dll) {
        startup_error=GetLastError();
        wsprintfA(text,"Falha libvlc.dll: Windows %lu.",startup_error);logline(text);return 0;
    }
    LOAD(libvlc_new); LOAD(libvlc_release); LOAD(libvlc_media_new_location);
    LOAD(libvlc_media_add_option); LOAD(libvlc_media_release); LOAD(libvlc_media_get_stats);
    LOAD(libvlc_media_player_new); LOAD(libvlc_media_player_set_media);
    LOAD(libvlc_media_player_set_hwnd); LOAD(libvlc_media_player_play);
    LOAD(libvlc_media_player_stop); LOAD(libvlc_media_player_release);
    LOAD(libvlc_media_player_get_state); LOAD(libvlc_media_player_has_vout);
    LOAD(libvlc_video_set_mouse_input); LOAD(libvlc_video_set_key_input);
    /* Respect the existing VLC profile that already plays both cameras. */
    DWORD envlen=GetEnvironmentVariableA("APPDATA",appdata,sizeof appdata);
    if(envlen>0 && envlen<sizeof appdata) {
        makepath(profile,sizeof profile,appdata,"vlc\\vlcrc");
        makepath(pathbuf,sizeof pathbuf,base,"camxp-vlc.cfg");
        /* Work on a separate profile. Never overwrite the user's VLC settings. */
        CopyFileA(profile,pathbuf,TRUE);
        if(GetFileAttributesA(pathbuf)!=INVALID_FILE_ATTRIBUTES) {
            copystr(config_arg,"--config=",sizeof config_arg);
            append(config_arg,pathbuf,sizeof config_arg);
            args[argc++]=config_arg;
            args[argc++]="--no-ignore-config";
            logline("Usando copia local do perfil VLC: camxp-vlc.cfg.");
        }
    }
    args[argc++]="--no-audio";
    args[argc++]="--no-osd";
    args[argc++]="--no-video-title-show";
    args[argc++]="--no-fullscreen";
    args[argc++]="--no-video-wallpaper";
    args[argc++]="--no-video-on-top";
    args[argc++]="--stats";
    args[argc++]="--quiet";
    if(!equal(renderer,"auto")) {
        copystr(render_arg,"--vout=",sizeof render_arg);
        append(render_arg,renderer,sizeof render_arg); args[argc++]=render_arg;
    }
    logline("Inicializando LibVLC e examinando plugins.");
    vlc=libvlc_new(argc,args);
    if(!vlc) {
        startup_error=GetLastError();
        logline("libvlc_new retornou NULL.");
    } else {logline("LibVLC inicializado.");}
    return vlc!=NULL;
}
static DWORD WINAPI stop_thread(void *context) {
    StopJob *job=(StopJob *)context;
    if(job->player) {
        libvlc_media_player_stop(job->player);
        libvlc_media_player_release(job->player);
    }
    if(job->media)libvlc_media_release(job->media);
    return 0;
}
static int retire_player(int side,unsigned delay) {
    after_stop[side]=delay;
    if(stopping[side])return 1;
    if(!player[side] && !media_ref[side]) {
        retry_at[side]=GetTickCount()+delay*1000u;
        waiting[side]=enabled[side] && !closing;return 1;
    }
    stopjob[side].player=player[side];stopjob[side].media=media_ref[side];
    stopping[side]=CreateThread(NULL,0,stop_thread,&stopjob[side],0,NULL);
    if(!stopping[side]) {
        logline("Nao foi possivel criar tarefa de encerramento. Player preservado.");
        return 0;
    }
    player[side]=NULL;media_ref[side]=NULL;waiting[side]=0;
    old_status[side][0]=0;
    return 1;
}
static void queue_retry(int side,const char *reason,int automatic) {
    unsigned delay=automatic?cam_retry_delay(attempts[side]):60;
    wsprintfA(text,"Camera %s: %s. Nova tentativa apos %u s do encerramento.",channels[side],reason,delay);
    logline(text);
    if(retire_player(side,delay)) {
        if(automatic && attempts[side]<4)attempts[side]++;
        wsprintfA(text,"Camera %s - Recuperando conexao...",channels[side]);
        SetWindowTextA(label[side],text);
        if(fullscreen)layout();
    }
}
static void reap_stops(void) {
    int menus_changed=0;
    for(int i=0;i<CAM_MAX;i++)if(stopping[i] && WaitForSingleObject(stopping[i],0)==WAIT_OBJECT_0) {
        CloseHandle(stopping[i]);stopping[i]=NULL;
        stopjob[i].player=NULL;stopjob[i].media=NULL;
        retry_at[i]=GetTickCount()+after_stop[i]*1000u;
        waiting[i]=enabled[i] && !closing;
        if(!enabled[i])menus_changed=1;
        wsprintfA(text,"Camera %s: sessao anterior encerrada.",channels[i]);logline(text);
    }
    if(menus_changed && !closing)rebuild_menus();
}
static void finish_close(void) {
    if(!closing)return;
    for(int i=0;i<CAM_MAX;i++)if(stopping[i] || player[i] || media_ref[i])return;
    if(vlc){libvlc_release(vlc);vlc=NULL;}
    DestroyWindow(mainwin);
}
static void toggle_fullscreen(int enable) {
    MONITORINFO monitor;
    if(enable==fullscreen || closing)return;
    if(enable) {
        clearbytes(&normal_placement,sizeof normal_placement);
        normal_placement.length=sizeof normal_placement;
        if(!GetWindowPlacement(mainwin,&normal_placement))return;
        monitor.cbSize=sizeof monitor;
        if(!GetMonitorInfoA(MonitorFromWindow(mainwin,MONITOR_DEFAULTTONEAREST),&monitor))return;
        normal_style=GetWindowLongA(mainwin,GWL_STYLE);
        fullscreen=1;SetMenu(mainwin,NULL);
        SetWindowLongA(mainwin,GWL_STYLE,(normal_style&~WS_OVERLAPPEDWINDOW)|WS_POPUP);
        SetWindowPos(mainwin,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,
            monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,
            SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        logline("Tela inteira ativada. F11/Esc retorna; Alt+F4 encerra.");
    } else {
        fullscreen=0;SetWindowLongA(mainwin,GWL_STYLE,normal_style);
        SetMenu(mainwin,main_menu);SetWindowPlacement(mainwin,&normal_placement);
        SetWindowPos(mainwin,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
        logline("Modo janela restaurado.");
    }
    layout();SetFocus(mainwin);
}
static void show_context(HWND hwnd,int x,int y) {
    HMENU popup=CreatePopupMenu();
    if(x==-1 && y==-1) {POINT point;GetCursorPos(&point);x=point.x;y=point.y;}
    AppendMenuA(popup,MF_STRING,103,fullscreen?"Voltar para janela (F11/Esc)":"Tela inteira (F11)");
    AppendMenuA(popup,MF_STRING,101,"Reconectar cameras");
    AppendMenuA(popup,MF_STRING,102,"Fechar (Alt+F4)");
    TrackPopupMenu(popup,TPM_RIGHTBUTTON,x,y,0,hwnd,NULL);DestroyMenu(popup);
}
static LRESULT CALLBACK panel_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_LBUTTONDOWN){SetFocus(mainwin);return 0;}
    if(msg==WM_LBUTTONDBLCLK){SendMessageA(mainwin,WM_COMMAND,103,0);return 0;}
    if(msg==WM_CONTEXTMENU){SendMessageA(mainwin,msg,wp,lp);return 0;}
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static int camera_count(void) {
    int count=0;for(int i=0;i<CAM_MAX;i++)if(enabled[i])count++;return count;
}
static int channel_selected(int channel,int except) {
    char value[16];wsprintfA(value,"%d",channel);
    for(int i=0;i<CAM_MAX;i++)if(i!=except &&
        (enabled[i] || stopping[i] || player[i] || media_ref[i]) && equal(value,channels[i]))return 1;
    return 0;
}
static void rebuild_menus(void) {
    HMENU old=main_menu,cameras=CreatePopupMenu(),add=CreatePopupMenu(),remove=CreatePopupMenu(),view=CreatePopupMenu(),layouts=CreatePopupMenu();
    main_menu=CreateMenu();
    int count=camera_count(),position=0;
    for(int ch=1;ch<=CAM_MAX;ch++) {
        wsprintfA(text,"Camera %d",ch);
        AppendMenuA(add,MF_STRING|(channel_selected(ch,-1)||count==CAM_MAX?MF_GRAYED:0),1000+ch,text);
    }
    AppendMenuA(cameras,MF_POPUP,(UINT_PTR)add,"Adicionar camera");
    for(int i=0;i<CAM_MAX;i++)if(enabled[i]) {
        wsprintfA(text,"Painel %d - Camera %s",++position,channels[i]);
        AppendMenuA(remove,MF_STRING,2000+i,text);
    }
    if(!count)AppendMenuA(remove,MF_GRAYED,0,"Nenhuma camera");
    AppendMenuA(cameras,MF_POPUP,(UINT_PTR)remove,"Remover camera");
    AppendMenuA(cameras,MF_SEPARATOR,0,NULL);
    position=0;
    for(int i=0;i<CAM_MAX;i++)if(enabled[i]) {
        HMENU selection=CreatePopupMenu();
        for(int ch=1;ch<=CAM_MAX;ch++) {
            char value[16];wsprintfA(value,"%d",ch);wsprintfA(text,"Camera %d",ch);
            AppendMenuA(selection,MF_STRING|(channel_selected(ch,i)?MF_GRAYED:0)|(equal(value,channels[i])?MF_CHECKED:0),3000+i*CAM_MAX+ch-1,text);
        }
        wsprintfA(text,"Painel %d - Camera %s",++position,channels[i]);
        AppendMenuA(cameras,MF_POPUP,(UINT_PTR)selection,text);
    }
    AppendMenuA(cameras,MF_SEPARATOR,0,NULL);
    AppendMenuA(cameras,MF_STRING,101,"Reconectar todas (aguarda 60 s)");
    AppendMenuA(cameras,MF_STRING,102,"Fechar");
    AppendMenuA(main_menu,MF_POPUP,(UINT_PTR)cameras,"Cameras");
    AppendMenuA(layouts,MF_STRING|(layout_mode==CAM_SIDE_BY_SIDE?MF_CHECKED:0),110,"Lado a lado");
    AppendMenuA(layouts,MF_STRING|(layout_mode==CAM_STACKED?MF_CHECKED:0),111,"Uma acima da outra");
    AppendMenuA(layouts,MF_STRING|(layout_mode==CAM_GRID?MF_CHECKED:0),112,"Mosaico automatico");
    AppendMenuA(view,MF_POPUP,(UINT_PTR)layouts,"Layout");
    AppendMenuA(view,MF_STRING,103,"Tela inteira\tF11");
    AppendMenuA(view,MF_STRING,104,"Voltar para janela\tEsc");
    AppendMenuA(main_menu,MF_POPUP,(UINT_PTR)view,"Exibir");
    if(mainwin && !fullscreen){SetMenu(mainwin,main_menu);DrawMenuBar(mainwin);}
    if(old)DestroyMenu(old);
}
static void set_layout(int mode) {
    if(closing || layout_mode==mode)return;
    int previous=layout_mode;layout_mode=mode;
    if(!save_settings()) {
        layout_mode=previous;MessageBoxA(mainwin,"Nao foi possivel salvar o layout.","SimpleSecCamViewer",MB_OK|MB_ICONERROR);return;
    }
    rebuild_menus();layout();
    logline("Layout alterado e salvo, sem reconectar as cameras.");
}
static void add_camera(int channel) {
    if(closing || channel<1 || channel>CAM_MAX || camera_count()>=CAM_MAX || channel_selected(channel,-1))return;
    int slot=-1;
    for(int i=0;i<CAM_MAX;i++)if(!enabled[i] && !stopping[i] && !player[i] && !media_ref[i]){slot=i;break;}
    if(slot<0){MessageBoxA(mainwin,"Aguarde o encerramento das cameras removidas antes de adicionar outra.","SimpleSecCamViewer",MB_OK|MB_ICONINFORMATION);return;}
    char previous[16];copystr(previous,channels[slot],sizeof previous);
    wsprintfA(channels[slot],"%d",channel);enabled[slot]=1;
    if(!save_settings()) {
        enabled[slot]=0;copystr(channels[slot],previous,sizeof channels[slot]);
        MessageBoxA(mainwin,"Nao foi possivel salvar a nova camera.","SimpleSecCamViewer",MB_OK|MB_ICONERROR);return;
    }
    attempts[slot]=0;old_status[slot][0]=0;
    rebuild_menus();queue_retry(slot,"camera adicionada e salva",0);layout();
}
static void remove_camera(int slot) {
    if(closing || !enabled[slot])return;
    enabled[slot]=0;
    if(!save_settings()) {
        enabled[slot]=1;MessageBoxA(mainwin,"Nao foi possivel salvar a remocao.","SimpleSecCamViewer",MB_OK|MB_ICONERROR);return;
    }
    waiting[slot]=0;
    if(!retire_player(slot,0)) {
        enabled[slot]=1;
        if(!save_settings())logline("Falha ao restaurar configuracao apos remocao interrompida.");
        MessageBoxA(mainwin,"Nao foi possivel encerrar essa camera agora. A remocao foi interrompida.","SimpleSecCamViewer",MB_OK|MB_ICONERROR);
        return;
    }
    rebuild_menus();layout();logline("Camera removida da exibicao; encerrando apenas essa sessao.");
}
static void change_channel(int side,int channel) {
    char value[16];
    if(closing || channel<1 || channel>CAM_MAX || !enabled[side] || channel_selected(channel,side))return;
    if(stopping[side]) {
        MessageBoxA(mainwin,"Aguarde o encerramento da sessao desse painel antes de trocar a camera.","SimpleSecCamViewer",MB_OK|MB_ICONINFORMATION);
        return;
    }
    wsprintfA(value,"%d",channel);
    if(equal(value,channels[side]))return;
    char previous[16];copystr(previous,channels[side],sizeof previous);
    copystr(channels[side],value,sizeof channels[side]);
    if(!save_settings()) {
        copystr(channels[side],previous,sizeof channels[side]);
        MessageBoxA(mainwin,"Nao foi possivel salvar camxp.ini. A troca nao foi aplicada.","SimpleSecCamViewer",MB_OK|MB_ICONERROR);return;
    }
    old_status[side][0]=0;attempts[side]=0;rebuild_menus();
    queue_retry(side,"canal alterado e salvo",0);

}
static void connectcam(int i) {
    Media *media;
    if(closing || !enabled[i] || player[i] || stopping[i])return;
    waiting[i]=0;stats_warned[i]=0;
    cam_watchdog_start(&watchdog[i],GetTickCount());
    /* All fields are bounded: this URL fits inside the 2048-byte buffer. */
    wsprintfA(url,"rtsp://%s:554/user=%s&password=%s&channel=%s&stream=1.sdp?real_stream",nvr,user,password,channels[i]);
    media=libvlc_media_new_location(vlc,url);
    clearbytes(url,sizeof url);
    if(!media) {queue_retry(i,"falha ao criar midia",1);return;}
    media_ref[i]=media;
    libvlc_media_add_option(media,":rtsp-tcp");
    libvlc_media_add_option(media,":network-caching=1000");
    player[i]=libvlc_media_player_new(vlc);
    if(player[i]) {
        libvlc_media_player_set_media(player[i],media);
        libvlc_media_player_set_hwnd(player[i],panel[i]);
        libvlc_video_set_mouse_input(player[i],0);
        libvlc_video_set_key_input(player[i],0);
        if(libvlc_media_player_play(player[i])<0) {queue_retry(i,"falha ao iniciar reproducao",1);return;}
    }
    if(!player[i]) {queue_retry(i,"falha ao criar player",1);return;}
    wsprintfA(text,"Canal %s: inicio solicitado, painel %d.",channels[i],i+1);
    logline(text);
}
static void update_status(void) {
    char status[128];
    DWORD now=GetTickCount();
    for(int i=0;i<CAM_MAX;i++) {
        if(!enabled[i])continue;
        int state=player[i]?libvlc_media_player_get_state(player[i]):0;
        unsigned vout=player[i]?libvlc_media_player_has_vout(player[i]):0;
        if(state<0 || state>7)state=7;
        if(stopping[i])wsprintfA(status,"Camera %s - Encerrando sessao...",channels[i]);
        else if(waiting[i]) {
            DWORD remaining=(LONG)(retry_at[i]-now)>0?(retry_at[i]-now+999u)/1000u:0;
            wsprintfA(status,"Camera %s - Conexao em %lu s",channels[i],remaining);
        } else wsprintfA(status,"Camera %s - %s%s",channels[i],states[state],state==3&&!vout?" (sem saida de video)":"");
        if(!equal(status,old_status[i])) {
            copystr(old_status[i],status,sizeof old_status[i]);SetWindowTextA(label[i],status);
            if(!waiting[i])logline(status);
        }
        if(player[i] && !closing) {
            MediaStats stats;
            clearbytes(&stats,sizeof stats);
            int available=media_ref[i] && libvlc_media_get_stats(media_ref[i],&stats);
            if(!available && !stats_warned[i]) {
                logline("Contadores de quadros indisponiveis; monitorando estado e saida de video.");stats_warned[i]=1;
            }
            int reason=cam_watchdog_sample(&watchdog[i],now,state,available,stats.displayed_pictures,vout!=0,IsIconic(mainwin));
            if(watchdog[i].healthy)attempts[i]=0;
            if(reason) {
                const char *why=reason==CAM_FROZEN?"quadros parados por 30 s":reason==CAM_NO_IMAGE?"sem primeiro quadro por 45 s":"conexao encerrada ou com erro";
                queue_retry(i,why,1);
            }
        }
        if(fullscreen && overlay_shown[i]!=(waiting[i] || stopping[i]!=NULL))layout();
    }
}
static void run_connections(void) {
    DWORD now=GetTickCount();
    for(int i=0;i<CAM_MAX;i++)if(enabled[i] && waiting[i] && !stopping[i] && cam_connection_due(now,retry_at[i],last_connect,made_connection)) {
        last_connect=now;made_connection=1;connectcam(i);break;
    }
}
static void layout(void) {
    RECT rect;GetClientRect(mainwin,&rect);
    int w=rect.right,h=rect.bottom,count=camera_count(),cols,rows,index=0;
    if(w<1 || h<1)return;
    int area_h=h-(fullscreen?0:26);if(area_h<1)area_h=1;
    cam_grid_size(count,layout_mode,w,area_h,fullscreen?0:24,&cols,&rows);
    for(int i=0;i<CAM_MAX;i++) {
        if(!enabled[i]){ShowWindow(panel[i],SW_HIDE);ShowWindow(label[i],SW_HIDE);overlay_shown[i]=-1;continue;}
        CamRect cell=cam_cell(index++,cols,rows,w,area_h);
        int notice=!fullscreen || waiting[i] || stopping[i]!=NULL;
        overlay_shown[i]=fullscreen?notice:-1;
        int header=notice?24:0;if(header>=cell.h)header=cell.h>1?cell.h-1:0;
        int gap=cell.w>2?1:0;
        MoveWindow(label[i],cell.x+gap,cell.y,cell.w-gap,header>0?header:1,TRUE);
        ShowWindow(label[i],notice?SW_SHOWNA:SW_HIDE);
        MoveWindow(panel[i],cell.x+gap,cell.y+header,cell.w-gap,cell.h-header,TRUE);
        ShowWindow(panel[i],SW_SHOWNA);
    }
    MoveWindow(footer,4,h-24,w>8?w-8:1,22,TRUE);
    ShowWindow(footer,fullscreen?SW_HIDE:SW_SHOWNA);
    MoveWindow(empty_notice,8,area_h/2,w>16?w-16:1,44,TRUE);
    ShowWindow(empty_notice,count?SW_HIDE:SW_SHOWNA);
}
static LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CONTEXTMENU:
        show_context(hwnd,(short)LOWORD(lp),(short)HIWORD(lp));return 0;
    case WM_SIZE: if(mainwin)layout(); return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x=520;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y=260; return 0;
    case WM_TIMER:
        if(wp==1) {
            reap_stops();
            if(closing){finish_close();return 0;}
            run_connections();update_status();
        } return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==103)toggle_fullscreen(!fullscreen);
        if(LOWORD(wp)==104)toggle_fullscreen(0);
        if(LOWORD(wp)>=110 && LOWORD(wp)<=112)set_layout(LOWORD(wp)-110);
        if(LOWORD(wp)>=1001 && LOWORD(wp)<=1016)add_camera(LOWORD(wp)-1000);
        if(LOWORD(wp)>=2000 && LOWORD(wp)<2000+CAM_MAX)remove_camera(LOWORD(wp)-2000);
        if(LOWORD(wp)>=3000 && LOWORD(wp)<3000+CAM_MAX*CAM_MAX) {
            int code=LOWORD(wp)-3000;change_channel(code/CAM_MAX,code%CAM_MAX+1);
        }
        if(LOWORD(wp)==101 && !closing) {
            for(int i=0;i<CAM_MAX;i++)if(enabled[i]){attempts[i]=0;queue_retry(i,"reconexao manual",0);}
        }
        if(LOWORD(wp)==102)SendMessageA(hwnd,WM_CLOSE,0,0);
        return 0;
    case WM_CLOSE:
        if(closing)return 0;
        closing=1;
        SetWindowTextA(footer,"Encerrando cameras...");
        logline("Encerrando players normalmente em segundo plano.");
        for(int i=0;i<CAM_MAX;i++){waiting[i]=0;retire_player(i,0);}
        finish_close();return 0;
    case WM_DESTROY: PostQuitMessage(0);return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
void WINAPI WinMainCRTStartup(void) {
    HINSTANCE instance=GetModuleHandleA(NULL);
    WNDCLASSA wc;
    MSG msg;
    HANDLE mutex;
    ACCEL keys[2];
    unsigned len;
    /* Same XP startup policy as VLC's bin/winvlc.c. Loader failures must
     * return to the caller so incompatible optional plugins can be skipped. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    mutex=CreateMutexA(NULL,FALSE,"CamXP.Native.TwoPanels.v1");
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND other=FindWindowA("CamXPNative",NULL);
        if(other){ShowWindow(other,SW_RESTORE);SetForegroundWindow(other);}
        ExitProcess(0);
    }
    GetModuleFileNameA(NULL,base,sizeof base); len=(unsigned)lstrlenA(base);
    while(len && base[len-1]!='\\')len--;
    if(!len)ExitProcess(1);
    base[len-1]=0;
    makepath(pathbuf,sizeof pathbuf,base,"camxp-app.log");
    log_file=CreateFileA(pathbuf,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    logline("SimpleSecCamViewer 1.4 - layouts e ate 16 cameras configuraveis.");
    if(!loadconfig()) {
        MessageBoxA(NULL,"Nao foi possivel ler a configuracao. Coloque iniciar.bat ao lado de SimpleSecCamViewer.exe e confira o NVR, usuario e caminho do VLC de 32 bits.","SimpleSecCamViewer",MB_OK|MB_ICONERROR);
        logline("Configuracao invalida ou libvlc.dll ausente."); ExitProcess(1);
    }
    if(needs_migration && !save_settings())
        logline("Nao foi possivel criar camxp.ini. Usando canais padrao nesta sessao.");
    if(!initvlc()) {
        wsprintfA(text,"Nao foi possivel carregar o VLC de 32 bits. Erro Windows: %lu. Consulte camxp-app.log.",startup_error);
        MessageBoxA(NULL,text,"SimpleSecCamViewer",MB_OK|MB_ICONERROR); logline("Falha ao carregar/inicializar LibVLC."); ExitProcess(1);
    }
    clearbytes(&wc,sizeof wc); wc.lpfnWndProc=wndproc; wc.hInstance=instance;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hIcon=LoadIconA(instance,MAKEINTRESOURCEA(1));
    if(!wc.hIcon)wc.hIcon=LoadIcon(NULL,IDI_APPLICATION);
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.lpszClassName="CamXPNative";
    RegisterClassA(&wc);
    wc.lpfnWndProc=panel_proc;wc.lpszClassName="CamXPVideoPanel";
    wc.style=CS_DBLCLKS;wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);RegisterClassA(&wc);
    rebuild_menus();
    mainwin=CreateWindowExA(0,"CamXPNative","SimpleSecCamViewer - Monitor de cameras",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,1000,520,NULL,main_menu,instance,NULL);
    if(!mainwin) {libvlc_release(vlc);ExitProcess(1);}
    for(int i=0;i<CAM_MAX;i++) {
        label[i]=CreateWindowExA(0,"STATIC","Aguardando",WS_CHILD|WS_VISIBLE,0,0,0,0,mainwin,NULL,instance,NULL);
        panel[i]=CreateWindowExA(0,"CamXPVideoPanel",NULL,WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
            0,0,0,0,mainwin,NULL,instance,NULL);
        SendMessageA(label[i],WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
    }
    footer=CreateWindowExA(0,"STATIC","F11: tela inteira | Esc: voltar | Reconexao automatica ativa",WS_CHILD|WS_VISIBLE,0,0,0,0,mainwin,NULL,instance,NULL);
    SendMessageA(footer,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
    empty_notice=CreateWindowExA(0,"STATIC","Nenhuma camera selecionada. Use Cameras > Adicionar camera.\r\nF11/Esc alterna o modo de tela inteira.",WS_CHILD|SS_CENTER,0,0,0,0,mainwin,NULL,instance,NULL);
    SendMessageA(empty_notice,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
    layout();ShowWindow(mainwin,SW_SHOWMAXIMIZED);UpdateWindow(mainwin);
    keys[0].fVirt=FVIRTKEY;keys[0].key=VK_F11;keys[0].cmd=103;
    keys[1].fVirt=FVIRTKEY;keys[1].key=VK_ESCAPE;keys[1].cmd=104;
    accelerators=CreateAcceleratorTableA(keys,2);
    logline("Janela e dois paineis criados.");
    int order=0;DWORD start=GetTickCount()+1000u;
    for(int i=0;i<CAM_MAX;i++)if(enabled[i]){waiting[i]=1;retry_at[i]=start+(order++)*3000u;}
    SetTimer(mainwin,1,1000,NULL);
    while(GetMessageA(&msg,NULL,0,0)>0) {
        if(!accelerators || !TranslateAcceleratorA(mainwin,accelerators,&msg)) {
            TranslateMessage(&msg);DispatchMessageA(&msg);
        }
    }
    if(accelerators)DestroyAcceleratorTable(accelerators);
    if(log_file!=INVALID_HANDLE_VALUE)CloseHandle(log_file);
    if(vlc_dll)FreeLibrary(vlc_dll);
    if(core_dll)FreeLibrary(core_dll);
    if(mutex)CloseHandle(mutex);
    ExitProcess(0);
}
