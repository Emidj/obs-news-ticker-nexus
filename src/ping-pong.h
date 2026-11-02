/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <math.h>
static inline double ping_pong_offset(double elapsed,double overflow,double speed,double pause)
{
    if(!isfinite(elapsed)||!isfinite(overflow)||!isfinite(speed)||!isfinite(pause)||elapsed<=0||overflow<=0||speed<=0)return 0;
    pause=fmax(0,pause);double travel=overflow/speed;
    double phase=fmod(elapsed,2*(pause+travel));
    if(phase<pause)return 0;
    phase-=pause;if(phase<travel)return phase*speed;
    phase-=travel;if(phase<pause)return overflow;
    return fmax(0,overflow-(phase-pause)*speed);
}
