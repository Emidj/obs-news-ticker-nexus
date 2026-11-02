/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "now-playing.h"
#include <obs.h>
#include <util/bmem.h>
#include <windows.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>
#include <stdio.h>

#define NP_BYTES 16384
#define NP_PATH 1024
#define NP_FILES 64
struct playing_lookup {obs_source_t *source;unsigned count;};
static bool find_playing(void *context,obs_source_t *source)
{
    struct playing_lookup *lookup=context;
    const char *id=obs_source_get_unversioned_id(source);
    if(id&&!strcmp(id,"obs_playing_next_development")){
        lookup->count++;if(!lookup->source)lookup->source=obs_source_get_ref(source);
    }
    return true;
}
void np_read_playing(struct np_sample *sample)
{
    memset(sample,0,sizeof(*sample));sample->status=NP_WAITING;
    struct playing_lookup lookup={0};obs_enum_sources(find_playing,&lookup);
    if(lookup.count>1)sample->status=NP_PROVIDER_AMBIGUOUS;
    else if(lookup.source){
        calldata_t args;calldata_init(&args);
        proc_handler_call(obs_source_get_proc_handler(lookup.source),"nexus_now_playing_demand_v1",&args);
        if(!proc_handler_call(obs_source_get_proc_handler(lookup.source),"nexus_now_playing_v1",&args))sample->status=NP_PROVIDER_UNSUPPORTED;
        else{
            const char *json=calldata_string(&args,"json");size_t length=json?strnlen(json,NP_BYTES+1):0;
            if(!length||length>NP_BYTES||!np_parse(json,length,sample))sample->status=NP_INVALID;
        }
        calldata_free(&args);
    }
    obs_source_release(lookup.source);
}
struct np_session {
    struct np_session *next;
    SRWLOCK lock;
    LONG refs;unsigned users;
    wchar_t path[NP_PATH];
    struct np_sample sample;
};
static SRWLOCK sessions_lock=SRWLOCK_INIT;
static struct np_session *sessions;
static HANDLE stop_event,wake_event,worker;

double np_now(void)
{
    FILETIME ft;GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value;value.LowPart=ft.dwLowDateTime;value.HighPart=ft.dwHighDateTime;
    return value.QuadPart/10000000.0-11644473600.0;
}
static bool text_valid(const char *s,size_t maximum,bool required)
{
    size_t length=strlen(s);bool content=false;
    if(length>maximum)return false;
    for(size_t i=0;i<length;i++) {
        unsigned char c=(unsigned char)s[i];if(c<32 || c==127)return false;
        if(!isspace(c))content=true;
    }
    return !required || content;
}
bool np_parse(const char *json,size_t size,struct np_sample *sample)
{
    memset(sample,0,sizeof(*sample));sample->status=NP_INVALID;
    if(!json || !size || size>NP_BYTES || memchr(json,0,size))return false;
    char *copy=bmalloc(size+1);memcpy(copy,json,size);copy[size]=0;
    obs_data_t *data=obs_data_create_from_json(copy);bfree(copy);
    if(!data)return false;
    bool valid=false;
    const char *schema=obs_data_get_string(data,"schema"),*state=obs_data_get_string(data,"state");
    double stamp=obs_data_get_double(data,"updated_at");
    if(strcmp(schema,"nexus.now-playing.v1") || !isfinite(stamp) || stamp<=0)goto done;
    enum np_status status=NP_INVALID;
    if(!strcmp(state,"playing"))status=NP_PLAYING;
    else if(!strcmp(state,"paused"))status=NP_PAUSED;
    else if(!strcmp(state,"stopped"))status=NP_STOPPED;
    else if(!strcmp(state,"waiting") || !strcmp(state,"unavailable"))status=NP_WAITING;
    if(status==NP_INVALID)goto done;
    if(status==NP_PLAYING) {
        const char *artist=obs_data_get_string(data,"artist"),*title=obs_data_get_string(data,"title");
        obs_data_item_t *artist_item=obs_data_item_byname(data,"artist");
        bool artist_type=!artist_item || obs_data_item_gettype(artist_item)==OBS_DATA_STRING;
        obs_data_item_release(&artist_item);
        if(!artist_type)goto done;
        if(!text_valid(artist,256,false) || !text_valid(title,512,true))goto done;
        if(text_valid(artist,256,true))snprintf(sample->text,sizeof(sample->text),"%s — %s",artist,title);
        else snprintf(sample->text,sizeof(sample->text),"%s",title);
    }
    sample->updated_at=stamp;sample->status=status;valid=true;
done:
    obs_data_release(data);return valid;
}
enum np_status np_current_status(const struct np_sample *sample,double now,unsigned max_age)
{
    if(sample->status!=NP_PLAYING && sample->status!=NP_PAUSED && sample->status!=NP_STOPPED && sample->status!=NP_WAITING)return sample->status;
    if(!sample->updated_at)return NP_WAITING;
    if(sample->updated_at>now+5)return NP_FUTURE;
    if(now-sample->updated_at>max_age)return NP_STALE;
    return sample->status;
}
static void unref(struct np_session *s)
{ if(InterlockedDecrement(&s->refs)==0)bfree(s); }
static void read_file(struct np_session *s,struct np_sample *sample)
{
    memset(sample,0,sizeof(*sample));sample->status=NP_IO_ERROR;
    wchar_t root[]={s->path[0],L':',L'\\',0};
    UINT drive=GetDriveTypeW(root);
    if(drive!=DRIVE_FIXED && drive!=DRIVE_REMOVABLE && drive!=DRIVE_RAMDISK) {sample->status=NP_BAD_PATH;return;}
    HANDLE file=CreateFileW(s->path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
    if(file==INVALID_HANDLE_VALUE) {
        DWORD error=GetLastError();if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND)sample->status=NP_MISSING;return;
    }
    LARGE_INTEGER length;
    if(!GetFileSizeEx(file,&length) || length.QuadPart<=0 || length.QuadPart>NP_BYTES) {sample->status=NP_INVALID;CloseHandle(file);return;}
    OVERLAPPED operation={0};operation.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    char buffer[NP_BYTES];DWORD bytes=0;
    bool ready=operation.hEvent && ReadFile(file,buffer,(DWORD)length.QuadPart,&bytes,&operation);
    if(ready)ready=GetOverlappedResult(file,&operation,&bytes,FALSE)!=0;
    if(!ready && operation.hEvent && GetLastError()==ERROR_IO_PENDING) {
        HANDLE events[]={stop_event,operation.hEvent};
        ready=WaitForMultipleObjects(2,events,FALSE,250)==WAIT_OBJECT_0+1;
        if(ready)ready=GetOverlappedResult(file,&operation,&bytes,FALSE)!=0;
        else {CancelIoEx(file,&operation);GetOverlappedResult(file,&operation,&bytes,TRUE);}
    }
    if(ready && bytes==(DWORD)length.QuadPart)np_parse(buffer,bytes,sample);
    if(operation.hEvent)CloseHandle(operation.hEvent);CloseHandle(file);
}
static DWORD WINAPI run(void *unused)
{
    (void)unused;
    while(WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0) {
        struct np_session *snapshot[NP_FILES];unsigned count=0;
        AcquireSRWLockShared(&sessions_lock);
        for(struct np_session *s=sessions;s && count<NP_FILES;s=s->next){InterlockedIncrement(&s->refs);snapshot[count++]=s;}
        ReleaseSRWLockShared(&sessions_lock);
        for(unsigned i=0;i<count;i++) {
            if(WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0) {
                struct np_sample sample;read_file(snapshot[i],&sample);
                AcquireSRWLockExclusive(&snapshot[i]->lock);snapshot[i]->sample=sample;ReleaseSRWLockExclusive(&snapshot[i]->lock);
            }
            unref(snapshot[i]);
        }
        HANDLE events[]={stop_event,wake_event};
        if(WaitForMultipleObjects(2,events,FALSE,count?500:INFINITE)==WAIT_OBJECT_0)break;
    }
    return 0;
}
bool np_start(void)
{
    stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);wake_event=CreateEventW(NULL,FALSE,FALSE,NULL);
    if(stop_event && wake_event)worker=CreateThread(NULL,0,run,NULL,0,NULL);
    if(worker)return true;
    if(stop_event)CloseHandle(stop_event);if(wake_event)CloseHandle(wake_event);
    stop_event=wake_event=NULL;return false;
}
void np_stop(void)
{
    if(!worker)return;
    SetEvent(stop_event);CancelSynchronousIo(worker);WaitForSingleObject(worker,INFINITE);
    CloseHandle(worker);CloseHandle(stop_event);CloseHandle(wake_event);worker=stop_event=wake_event=NULL;
}
struct np_session *np_acquire(const char *path)
{
    if(!worker || !path || !*path)return NULL;
    wchar_t input[NP_PATH],canonical[NP_PATH];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,input,NP_PATH) || !iswalpha(input[0]) || input[1]!=L':' || (input[2]!=L'\\' && input[2]!=L'/'))return NULL;
    DWORD length=GetFullPathNameW(input,NP_PATH,canonical,NULL);
    if(!length || length>=NP_PATH)return NULL;
    AcquireSRWLockExclusive(&sessions_lock);
    unsigned count=0;
    for(struct np_session *s=sessions;s;s=s->next) {
        count++;
        if(CompareStringOrdinal(s->path,-1,canonical,-1,TRUE)==CSTR_EQUAL) {
            s->users++;InterlockedIncrement(&s->refs);ReleaseSRWLockExclusive(&sessions_lock);return s;
        }
    }
    if(count>=NP_FILES){ReleaseSRWLockExclusive(&sessions_lock);return NULL;}
    struct np_session *s=bzalloc(sizeof(*s));InitializeSRWLock(&s->lock);s->refs=1;s->users=1;
    memcpy(s->path,canonical,(length+1)*sizeof(wchar_t));s->sample.status=NP_WAITING;s->next=sessions;sessions=s;
    ReleaseSRWLockExclusive(&sessions_lock);SetEvent(wake_event);return s;
}
void np_release(struct np_session *s)
{
    if(!s)return;
    AcquireSRWLockExclusive(&sessions_lock);
    if(--s->users==0){struct np_session **link=&sessions;while(*link && *link!=s)link=&(*link)->next;if(*link)*link=s->next;}
    ReleaseSRWLockExclusive(&sessions_lock);unref(s);if(wake_event)SetEvent(wake_event);
}
void np_read(struct np_session *s,struct np_sample *sample)
{
    memset(sample,0,sizeof(*sample));sample->status=NP_WAITING;if(!s)return;
    AcquireSRWLockShared(&s->lock);*sample=s->sample;ReleaseSRWLockShared(&s->lock);
}
