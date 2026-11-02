/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <math.h>

/* Fraction of a card revealed from its left-hand anchor. Times are seconds. */
static inline double block_reveal(double time,double transition,double hold,double pause)
{
    if(!isfinite(time) || transition<=0 || hold<0 || pause<0) return 0;
    double cycle=transition*2+hold+pause;
    time=fmod(time,cycle); if(time<0) time+=cycle;
    if(time<transition) {
        double p=time/transition; return p*p*(3-2*p);
    }
    time-=transition;
    if(time<hold) return 1;
    time-=hold;
    if(time<transition) {
        double p=time/transition; return 1-p*p*(3-2*p);
    }
    return 0;
}
