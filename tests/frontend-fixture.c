/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only OBS frontend lifecycle. Never distributed. */
enum obs_frontend_event;
__declspec(dllexport) void obs_frontend_add_event_callback(void (*cb)(enum obs_frontend_event,void *),void *data);
__declspec(dllexport) void obs_frontend_remove_event_callback(void (*cb)(enum obs_frontend_event,void *),void *data);
#include <obs-frontend-api.h>
static obs_frontend_event_cb callback;
static void *context;
__declspec(dllexport) void obs_frontend_add_event_callback(obs_frontend_event_cb cb,void *data)
{callback=cb;context=data;}
__declspec(dllexport) void obs_frontend_remove_event_callback(obs_frontend_event_cb cb,void *data)
{if(callback==cb&&context==data){callback=NULL;context=NULL;}}
__declspec(dllexport) void fixture_frontend_emit(enum obs_frontend_event event)
{if(callback)callback(event,context);}
__declspec(dllexport) int fixture_frontend_subscribers(void)
{return callback!=NULL;}
