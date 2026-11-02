/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <obs.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/weather.h"
#include "../src/now-playing.h"
static int checks,failures;
static void check(const char *name,bool ok){printf("%s %s\n",ok?"PASS":"FAIL",name);checks++;failures+=!ok;}
static void current(char *out,const char *temperature,const char *code,const char *day,const char *unit,double time)
{
    snprintf(out,2048,"{\"current_units\":{\"time\":\"unixtime\",\"temperature_2m\":\"%s\"},\"current\":{\"time\":%.0f,\"temperature_2m\":%s,\"weather_code\":%s,\"is_day\":%s}}",unit,time,temperature,code,day);
}
int main(int argc,char **argv)
{
    if(!obs_startup("en-US",NULL,NULL))return 2;
    const char *location="{\"results\":[{\"name\":\"Madrid\",\"country_code\":\"ES\",\"latitude\":40.4,\"longitude\":-3.7}]}";
    double lat,lon;char city[192],json[2048],text[512];struct weather_sample sample;
    check("resolved city includes country",weather_parse_location(location,strlen(location),&lat,&lon,city,sizeof(city))&&!strcmp(city,"Madrid (ES)")&&lat==40.4&&lon==-3.7);
    check("empty geocoding results rejected",!weather_parse_location("{}",2,&lat,&lon,city,sizeof(city))&&!city[0]);
    check("location without coordinates rejected",!weather_parse_location("{\"results\":[{\"name\":\"Madrid\",\"country_code\":\"ES\"}]}",54,&lat,&lon,city,sizeof(city)));
    check("truncated JSON rejected",!weather_parse_location(location,10,&lat,&lon,city,sizeof(city)));
    check("small city destination rejected",!weather_parse_location(location,strlen(location),&lat,&lon,city,3));
    current(json,"21.4","0","1","°C",10000);check("valid model current accepted",weather_parse_current(json,strlen(json),10000,&sample)&&sample.temperature==21.4&&sample.code==0&&sample.day);
    snprintf(sample.city,sizeof(sample.city),"Madrid (ES)");weather_format(&sample,10000,text,sizeof(text));check("temperature and city without embedded provider name",strstr(text,"21°C Madrid (ES)")&&!strstr(text,"Open-Meteo"));
    weather_format(&sample,12000,text,sizeof(text));check("cached value expires",!text[0]);
    weather_format(&sample,9000,text,sizeof(text));check("future value hidden",!text[0]);
    current(json,"null","0","1","°C",10000);check("null temperature rejected",!weather_parse_current(json,strlen(json),10000,&sample));
    current(json,"\"21\"","0","1","°C",10000);check("string temperature rejected",!weather_parse_current(json,strlen(json),10000,&sample));
    current(json,"21","0","1","°F",10000);check("wrong unit rejected",!weather_parse_current(json,strlen(json),10000,&sample));
    current(json,"200","0","1","°C",10000);check("impossible temperature rejected",!weather_parse_current(json,strlen(json),10000,&sample));
    current(json,"21","4","1","°C",10000);check("unknown WMO code rejected",!weather_parse_current(json,strlen(json),10000,&sample));
    current(json,"21","1e100","1","°C",10000);check("huge weather code rejected before conversion",!weather_parse_current(json,strlen(json),10000,&sample));
    current(json,"21","61","2","°C",10000);check("invalid day flag rejected",!weather_parse_current(json,strlen(json),10000,&sample));
    current(json,"21","61","0","°C",7000);check("old provider timestamp rejected",!weather_parse_current(json,strlen(json),10000,&sample)&&sample.status==WEATHER_STALE);
    current(json,"21","61","0","°C",11000);check("future provider timestamp rejected",!weather_parse_current(json,strlen(json),10000,&sample)&&sample.status==WEATHER_STALE);
    current(json,"-3","73","0","°C",10000);check("snow and negative temperature accepted",weather_parse_current(json,strlen(json),10000,&sample)&&sample.code==73&&!sample.day);
    check("worker starts without network subscriptions",weather_start());
    check("blank city rejected",!weather_acquire("   "));
    check("control characters rejected",!weather_acquire("Madrid\n"));
    char invalid[]={ (char)0xff,0 };check("invalid UTF8 city rejected",!weather_acquire(invalid));
    bool ready[WEATHER_CITIES]={true,false,true,false,true};struct weather_rotation rotation={0};
    check("rotation holds first city for interval",weather_rotate(&rotation,ready,5,4,true,5)==0);
    check("rotation skips unavailable city",weather_rotate(&rotation,ready,5,1,true,5)==2);
    check("hidden rotation clock pauses",weather_rotate(&rotation,ready,5,100,false,5)==2&&rotation.elapsed==0);
    check("rotation advances to last valid slot",weather_rotate(&rotation,ready,5,5,true,5)==4);
    check("rotation wraps to first city",weather_rotate(&rotation,ready,5,5,true,5)==0);
    ready[0]=false;check("expired current city is replaced immediately",weather_rotate(&rotation,ready,5,0,true,5)==2);
    ready[2]=false;check("one healthy city remains stable",weather_rotate(&rotation,ready,5,100,true,5)==4);
    ready[4]=false;check("no healthy cities resets timer",weather_rotate(&rotation,ready,5,3,true,5)==4&&rotation.elapsed==0);
    ready[0]=true;ready[2]=true;ready[4]=true;rotation=(struct weather_rotation){0};
    check("long frame advances correct number of cities",weather_rotate(&rotation,ready,5,12,true,5)==4&&rotation.elapsed==2);
    check("single city ignores extra configured slots",weather_rotate(&rotation,ready,1,99,true,5)==0);
    check("empty list resets selection",weather_rotate(&rotation,ready,0,5,true,5)==0&&rotation.elapsed==0);
    const char *expected[]={"Madrid (ES)","Londres (GB)","París (FR)","Roma (IT)","Tokio (JP)","Nueva Delhi (IN)","Washington D. C. (US)","Abu Dabi (AE)","Seúl (KR)","Pekín (CN)","Buenos Aires (AR)","Bogotá (CO)","Santiago de Chile (CL)","Brasilia (BR)","Ciudad de México (MX)","Ottawa (CA)","Berlín (DE)","Ámsterdam (NL)","Bruselas (BE)","Varsovia (PL)","Lisboa (PT)","Viena (AT)","Atenas (GR)","Estocolmo (SE)"};
    check("exactly twenty-four default capitals",weather_capital_count()==24);
    for(unsigned i=0;i<24;i++){
        bool ok=!strcmp(weather_capital_selection(i),expected[i])&&weather_capital_location(expected[i],&lat,&lon,city,sizeof(city))&&!strcmp(city,expected[i])&&lat>=-90&&lat<=90&&lon>=-180&&lon<=180;
        check(expected[i],ok);
    }
    memset(ready,0,sizeof(ready));for(unsigned i=0;i<24;i++)ready[i]=true;rotation=(struct weather_rotation){0};bool ordered=true;
    for(unsigned i=1;i<=24;i++)if(weather_rotate(&rotation,ready,WEATHER_CITIES,15,true,15)!=i%24)ordered=false;
    check("all twenty-four capitals rotate in order and wrap",ordered);
    check("capital lookup rejects unknown custom city",!weather_capital_location("Unknown custom town",&lat,&lon,city,sizeof(city)));
    if(argc>1&&!strcmp(argv[1],"--capitals-live")){
        struct weather_session *slots[24];bool shared=true;
        for(unsigned i=0;i<24;i++)slots[i]=weather_acquire(weather_capital_selection(i));
        check("all twenty-four cities fit shared worker capacity",slots[23]!=NULL);
        for(unsigned i=0;i<24;i++){struct weather_session *duplicate=weather_acquire(weather_capital_selection(i));if(!duplicate||duplicate!=slots[i])shared=false;weather_release(duplicate);}
        check("second source reuses all twenty-four sessions",shared);
        for(int attempt=0;attempt<900;attempt++){
            bool pending=false;for(unsigned i=0;i<24;i++){weather_read(slots[i],&sample);if(sample.status==WEATHER_WAITING)pending=true;}
            if(!pending)break;Sleep(100);
        }
        for(unsigned i=0;i<24;i++){
            weather_read(slots[i],&sample);weather_format(&sample,np_now(),text,sizeof(text));printf("CAPITAL %u status=%d %s\n",i+1,sample.status,text);
            check(weather_capital_selection(i),sample.status==WEATHER_READY&&!strcmp(sample.city,weather_capital_selection(i))&&*text);weather_release(slots[i]);
        }
    }
    if(argc>1&&!strcmp(argv[1],"--live")){
        struct weather_session *a=weather_acquire("Madrid, Spain"),*b=weather_acquire("Madrid, Spain");
        check("same city shares a session",a&&a==b);weather_release(a);
        for(int i=0;i<300;i++){Sleep(100);weather_read(b,&sample);if(sample.status!=WEATHER_WAITING)break;}
        weather_format(&sample,np_now(),text,sizeof(text));printf("LIVE status=%d text=%s\n",sample.status,text);
        check("real HTTPS geocoding and current weather",sample.status==WEATHER_READY&&strstr(sample.city,"Madrid")&&!strstr(text,"Open-Meteo"));
        weather_release(b);
    }
    weather_stop();obs_shutdown();printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
