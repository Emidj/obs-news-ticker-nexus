/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "weather.h"
#include "ticker-version.h"
#define NEXUS_WIDE_IMPL(x) L##x
#define NEXUS_WIDE(x) NEXUS_WIDE_IMPL(x)
#include "now-playing.h"
#include "weather-capitals.h"
#include <obs.h>
#include <util/bmem.h>
#include <windows.h>
#include <winhttp.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define WEATHER_BYTES 65536
#define WEATHER_SESSIONS WEATHER_CITIES
struct weather_session {
    struct weather_session *next;
    SRWLOCK lock;
    LONG refs;unsigned users;
    char query[129];double next_poll;
    struct weather_sample sample;
};
static SRWLOCK sessions_lock=SRWLOCK_INIT;
static struct weather_session *sessions;
static HANDLE stop_event,wake_event,worker;
unsigned weather_rotate(struct weather_rotation *r,const bool ready[WEATHER_CITIES],unsigned count,double seconds,bool showing,unsigned interval)
{
    if(count>WEATHER_CITIES)count=WEATHER_CITIES;
    if(!count){r->index=0;r->elapsed=0;return 0;}
    if(r->index>=count){r->index=0;r->elapsed=0;}
    unsigned available[WEATHER_CITIES],n=0,position=0;
    for(unsigned i=0;i<count;i++)if(ready[i]){if(i==r->index)position=n;available[n++]=i;}
    if(!n){r->elapsed=0;return r->index;}
    if(!ready[r->index]){r->index=available[0];r->elapsed=0;return r->index;}
    if(showing&&isfinite(seconds)&&seconds>0)r->elapsed+=seconds;
    if(!interval)interval=15;
    if(r->elapsed>=interval){
        unsigned steps=(unsigned)fmod(floor(r->elapsed/interval),(double)n);
        r->index=available[(position+steps)%n];r->elapsed=fmod(r->elapsed,(double)interval);
    }
    return r->index;
}
unsigned weather_capital_count(void){return (unsigned)(sizeof(weather_capitals)/sizeof(weather_capitals[0]));}
const char *weather_capital_selection(unsigned index){return index<weather_capital_count()?weather_capitals[index].selection:"";}
static const struct weather_capital *find_capital(const char *query)
{
    for(unsigned i=0;i<weather_capital_count();i++){
        const struct weather_capital *c=&weather_capitals[i];
        if(!_stricmp(query,c->selection)||!_stricmp(query,c->query)||!_stricmp(query,c->name))return c;
    }
    return NULL;
}
bool weather_capital_location(const char *query,double *latitude,double *longitude,char *city,size_t capacity)
{
    const struct weather_capital *c=find_capital(query);if(!c)return false;
    *latitude=c->latitude;*longitude=c->longitude;
    int n=snprintf(city,capacity,"%s",c->selection);return n>0&&(size_t)n<capacity;
}

static bool valid_text(const char *s,size_t max)
{
    size_t n=strlen(s);bool content=false;
    if(!n||n>max)return false;
    for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)s[i];if(c<32||c==127)return false;if(!isspace(c))content=true;}
    wchar_t wide[256];return content&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,wide,256)>0;
}
static obs_data_t *parse(const char *json,size_t length)
{
    if(!json||!length||length>WEATHER_BYTES||memchr(json,0,length))return NULL;
    char *copy=bmalloc(length+1);memcpy(copy,json,length);copy[length]=0;
    obs_data_t *data=obs_data_create_from_json(copy);bfree(copy);return data;
}
static bool number(obs_data_t *data,const char *key,double *value)
{
    obs_data_item_t *item=obs_data_item_byname(data,key);
    bool ok=item&&obs_data_item_gettype(item)==OBS_DATA_NUMBER;
    obs_data_item_release(&item);*value=obs_data_get_double(data,key);return ok&&isfinite(*value);
}
bool weather_parse_location(const char *json,size_t length,double *latitude,double *longitude,char *city,size_t capacity)
{
    if(capacity)city[0]=0;
    obs_data_t *data=parse(json,length);if(!data)return false;
    obs_data_array_t *results=obs_data_get_array(data,"results");
    obs_data_t *first=obs_data_array_count(results)?obs_data_array_item(results,0):NULL;
    const char *name=obs_data_get_string(first,"name"),*country=obs_data_get_string(first,"country_code");
    bool ok=first&&!obs_data_get_bool(data,"error")&&number(first,"latitude",latitude)&&number(first,"longitude",longitude)&&fabs(*latitude)<=90&&fabs(*longitude)<=180&&valid_text(name,128)&&valid_text(country,2)&&strlen(country)==2;
    if(ok){int n=snprintf(city,capacity,"%s (%s)",name,country);ok=n>0&&(size_t)n<capacity;}
    if(!ok&&capacity)city[0]=0;
    obs_data_release(first);obs_data_array_release(results);obs_data_release(data);return ok;
}
bool weather_parse_current(const char *json,size_t length,double now,struct weather_sample *sample)
{
    memset(sample,0,sizeof(*sample));sample->status=WEATHER_INVALID;
    obs_data_t *data=parse(json,length);if(!data)return false;
    obs_data_t *current=obs_data_get_obj(data,"current"),*units=obs_data_get_obj(data,"current_units");
    double code,day;
    bool ok=current&&units&&!obs_data_get_bool(data,"error")&&number(current,"temperature_2m",&sample->temperature)&&sample->temperature>=-100&&sample->temperature<=70&&number(current,"time",&sample->time)&&number(current,"weather_code",&code)&&floor(code)==code&&code>=0&&code<=99&&number(current,"is_day",&day)&&(day==0||day==1)&&!strcmp(obs_data_get_string(units,"temperature_2m"),"°C")&&!strcmp(obs_data_get_string(units,"time"),"unixtime");
    if(ok){
        int c=(int)code;
        ok=c==0||c==1||c==2||c==3||c==45||c==48||c==51||c==53||c==55||c==56||c==57||c==61||c==63||c==65||c==66||c==67||c==71||c==73||c==75||c==77||c==80||c==81||c==82||c==85||c==86||c==95||c==96||c==99;
        sample->code=c;sample->day=day==1;
        if(sample->time>now+300||sample->time<now-1800){ok=false;sample->status=WEATHER_STALE;}
    }
    if(ok)sample->status=WEATHER_READY;
    obs_data_release(current);obs_data_release(units);obs_data_release(data);return ok;
}
void weather_format(const struct weather_sample *s,double now,char *text,size_t capacity)
{
    if(!capacity)return;text[0]=0;
    if(s->status!=WEATHER_READY||s->time>now+300||s->time<now-1800)return;
    const char *symbol=s->code==0?(s->day?"☀":"☾"):s->code<=3?"☁":s->code>=95?"ϟ":s->code>=71&&s->code<=77?"❄":s->code==85||s->code==86?"❄":s->code==45||s->code==48?"≋":"☂";
    char english[160];const char *city=s->city;
    const struct weather_capital *capital=find_capital(city);
    if(capital){
        size_t length=strcspn(capital->query,",");
        const char *country=strrchr(capital->selection,'(');
        snprintf(english,sizeof(english),"%.*s%s%s",(int)length,capital->query,country?" ":"",country?country:"");city=english;
    }
    snprintf(text,capacity,"%s %.0f°C %s",symbol,s->temperature,city);
}
/* Only the worker owns HTTP handles. No network, JSON or locks spanning I/O on the OBS threads. */
static bool request(const wchar_t *host,const wchar_t *path,char *body,size_t *length)
{
    bool ok=false;*length=0;
    HINTERNET session=WinHttpOpen(L"OBS-News-Ticker-NEXUS/" NEXUS_WIDE(NEXUS_TICKER_RELEASE),WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)return false;
    WinHttpSetTimeouts(session,2000,2000,2000,2000);
    HINTERNET connection=WinHttpConnect(session,host,INTERNET_DEFAULT_HTTPS_PORT,0);
    HINTERNET req=connection?WinHttpOpenRequest(connection,L"GET",path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):NULL;
    if(req){
        DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(req,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));
        if(WinHttpSendRequest(req,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(req,NULL)){
            DWORD status=0,bytes=sizeof(status);WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&bytes,WINHTTP_NO_HEADER_INDEX);
            ULONGLONG deadline=GetTickCount64()+8000;
            if(status==200)while(*length<WEATHER_BYTES&&GetTickCount64()<deadline&&WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0){
                DWORD read=0;if(!WinHttpReadData(req,body+*length,(DWORD)(WEATHER_BYTES-*length),&read))break;
                if(!read){ok=true;break;}*length+=read;
            }
        }
        WinHttpCloseHandle(req);
    }
    if(connection)WinHttpCloseHandle(connection);WinHttpCloseHandle(session);body[*length]=0;return ok;
}
static void unref(struct weather_session *s){if(!InterlockedDecrement(&s->refs))bfree(s);}
static void fetch(struct weather_session *s)
{
    char encoded[129*3+1];size_t used=0;
    for(const unsigned char *p=(const unsigned char*)s->query;*p;p++){
        if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr("-._~",*p))encoded[used++]=(char)*p;
        else{snprintf(encoded+used,4,"%%%02X",*p);used+=3;}
    }
    encoded[used]=0;wchar_t path[1024];
    swprintf(path,1024,L"/v1/search?name=%hs&count=1&language=en&format=json",encoded);
    char *body=bmalloc(WEATHER_BYTES+1);size_t length;double lat=0,lon=0;char city[192];
    struct weather_sample sample={.status=WEATHER_NETWORK};
    bool located=weather_capital_location(s->query,&lat,&lon,city,sizeof(city));
    if(!located&&request(L"geocoding-api.open-meteo.com",path,body,&length)){
        located=weather_parse_location(body,length,&lat,&lon,city,sizeof(city));
        if(!located)sample.status=WEATHER_LOCATION;
    }
    if(located&&WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0){
        swprintf(path,1024,L"/v1/forecast?latitude=%.6f&longitude=%.6f&current=temperature_2m,weather_code,is_day&temperature_unit=celsius&timeformat=unixtime&forecast_days=1",lat,lon);
        if(request(L"api.open-meteo.com",path,body,&length)){
            weather_parse_current(body,length,np_now(),&sample);snprintf(sample.city,sizeof(sample.city),"%s",city);
        }
    }
    bfree(body);
    AcquireSRWLockExclusive(&s->lock);s->sample=sample;ReleaseSRWLockExclusive(&s->lock);
    s->next_poll=GetTickCount64()/1000.0+(sample.status==WEATHER_READY?600:120);
}
static DWORD WINAPI run(void *unused)
{
    (void)unused;
    while(WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0){
        struct weather_session *snapshot[WEATHER_SESSIONS];unsigned count=0;
        AcquireSRWLockShared(&sessions_lock);
        for(struct weather_session *s=sessions;s&&count<WEATHER_SESSIONS;s=s->next){InterlockedIncrement(&s->refs);snapshot[count++]=s;}
        ReleaseSRWLockShared(&sessions_lock);
        for(unsigned i=0;i<count;i++){
            AcquireSRWLockShared(&sessions_lock);bool active=snapshot[i]->users>0;ReleaseSRWLockShared(&sessions_lock);
            if(active&&WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0&&GetTickCount64()/1000.0>=snapshot[i]->next_poll)fetch(snapshot[i]);
            unref(snapshot[i]);
        }
        HANDLE events[]={stop_event,wake_event};if(WaitForMultipleObjects(2,events,FALSE,count?1000:INFINITE)==WAIT_OBJECT_0)break;
    }
    return 0;
}
bool weather_start(void)
{
    stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);wake_event=CreateEventW(NULL,FALSE,FALSE,NULL);
    if(stop_event&&wake_event)worker=CreateThread(NULL,0,run,NULL,0,NULL);
    if(worker)return true;
    if(stop_event)CloseHandle(stop_event);if(wake_event)CloseHandle(wake_event);stop_event=wake_event=NULL;return false;
}
void weather_stop(void)
{
    if(!worker)return;SetEvent(stop_event);WaitForSingleObject(worker,INFINITE);
    CloseHandle(worker);CloseHandle(stop_event);CloseHandle(wake_event);worker=stop_event=wake_event=NULL;
}
struct weather_session *weather_acquire(const char *city)
{
    if(!worker||!city||!valid_text(city,128))return NULL;
    const struct weather_capital *capital=find_capital(city);if(capital)city=capital->selection;
    AcquireSRWLockExclusive(&sessions_lock);unsigned count=0;
    for(struct weather_session *s=sessions;s;s=s->next){count++;if(!_stricmp(s->query,city)){s->users++;InterlockedIncrement(&s->refs);ReleaseSRWLockExclusive(&sessions_lock);return s;}}
    if(count>=WEATHER_SESSIONS){ReleaseSRWLockExclusive(&sessions_lock);return NULL;}
    struct weather_session *s=bzalloc(sizeof(*s));InitializeSRWLock(&s->lock);s->users=1;s->refs=1;snprintf(s->query,sizeof(s->query),"%s",city);s->sample.status=WEATHER_WAITING;s->next_poll=GetTickCount64()/1000.0+1.5;struct weather_session **tail=&sessions;while(*tail)tail=&(*tail)->next;*tail=s;
    ReleaseSRWLockExclusive(&sessions_lock);SetEvent(wake_event);return s;
}
void weather_release(struct weather_session *s)
{
    if(!s)return;AcquireSRWLockExclusive(&sessions_lock);
    if(!--s->users){struct weather_session **link=&sessions;while(*link&&*link!=s)link=&(*link)->next;if(*link)*link=s->next;}
    ReleaseSRWLockExclusive(&sessions_lock);unref(s);if(wake_event)SetEvent(wake_event);
}
void weather_read(struct weather_session *s,struct weather_sample *sample)
{
    memset(sample,0,sizeof(*sample));sample->status=WEATHER_WAITING;if(!s)return;
    AcquireSRWLockShared(&s->lock);*sample=s->sample;ReleaseSRWLockShared(&s->lock);
    if(sample->status==WEATHER_READY&&(sample->time<np_now()-1800||sample->time>np_now()+300))sample->status=WEATHER_STALE;
}
