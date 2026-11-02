/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>

enum np_status { NP_DISABLED,NP_WAITING,NP_PLAYING,NP_PAUSED,NP_STOPPED,NP_MISSING,NP_INVALID,NP_STALE,NP_BAD_PATH,NP_IO_ERROR,NP_FUTURE,NP_PROVIDER_UNSUPPORTED,NP_PROVIDER_AMBIGUOUS };
struct np_sample { enum np_status status;double updated_at;char text[1024]; };
struct np_session;
bool np_start(void);
void np_stop(void);
struct np_session *np_acquire(const char *path);
void np_release(struct np_session *session);
void np_read(struct np_session *session,struct np_sample *sample);
double np_now(void);
bool np_parse(const char *json,size_t size,struct np_sample *sample);
void np_read_playing(struct np_sample *sample);
enum np_status np_current_status(const struct np_sample *sample,double now,unsigned max_age);
