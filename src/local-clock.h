/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

static inline bool local_clock_format(char *out,size_t size,unsigned hour,unsigned minute,unsigned second,bool twelve,bool seconds)
{
    if(!out || !size) return false;
    out[0]='\0';
    if(hour>23 || minute>59 || second>59) return false;
    const char *suffix=twelve?(hour<12?" AM":" PM"):"";
    unsigned display_hour=twelve?(hour%12?hour%12:12):hour;
    int written=seconds?snprintf(out,size,"%02u:%02u:%02u%s",display_hour,minute,second,suffix)
                       :snprintf(out,size,"%02u:%02u%s",display_hour,minute,suffix);
    return written>=0 && (size_t)written<size;
}
