/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
enum weather_status { WEATHER_MANUAL, WEATHER_WAITING, WEATHER_READY, WEATHER_LOCATION, WEATHER_NETWORK, WEATHER_INVALID, WEATHER_STALE };
struct weather_sample { enum weather_status status; double time,temperature; int code; bool day; char city[192]; };
struct weather_session;
#define WEATHER_CITIES 64
unsigned weather_capital_count(void);
const char *weather_capital_selection(unsigned index);
bool weather_capital_location(const char *query,double *latitude,double *longitude,char *city,size_t capacity);
struct weather_rotation { unsigned index; double elapsed; };
unsigned weather_rotate(struct weather_rotation *rotation,const bool ready[WEATHER_CITIES],unsigned count,double seconds,bool showing,unsigned interval);
bool weather_start(void);
void weather_stop(void);
struct weather_session *weather_acquire(const char *city);
void weather_release(struct weather_session *session);
void weather_read(struct weather_session *session,struct weather_sample *sample);
bool weather_parse_location(const char *json,size_t length,double *latitude,double *longitude,char *city,size_t capacity);
bool weather_parse_current(const char *json,size_t length,double now,struct weather_sample *sample);
void weather_format(const struct weather_sample *sample,double now,char *text,size_t capacity);
