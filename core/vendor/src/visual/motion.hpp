// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
namespace oto::visual {
enum class Motion { None, Fade, SlideFade, StyleChange };
struct MotionSample { float opacity = 1, offsetY = 0; bool finished = true; };
struct ChangeSample {
	float progress = 1, glow = 0, sweep = 0, particles = 0;
	float oldOpacity = 0, newOpacity = 1, oldOffsetY = 0, newOffsetY = 0;
	bool finished = true;
};
inline float smooth(float t) noexcept { t = std::clamp(t,0.0f,1.0f); return t*t*(3-2*t); }
// One finite timeline: entrance, name transition/sweep, residual glow, fade out.
// The initial state and the end are exact; no timer, resource allocation or replay.
inline ChangeSample sampleChange(double elapsedMs, double durationMs) noexcept
{
	if (!std::isfinite(elapsedMs) || !std::isfinite(durationMs) || durationMs <= 0 || elapsedMs >= durationMs) return {};
	const float t = float(std::clamp(elapsedMs/durationMs,0.0,1.0));
	const float transition = smooth((t-0.12f)/0.30f);
	const float outgoing = smooth((t-0.10f)/0.12f);
	const float glow = t < 0.12f ? smooth(t/0.12f) : t < 0.44f ? 1.0f :
		t < 0.70f ? 1.0f-0.65f*smooth((t-0.44f)/0.26f) : 0.35f*(1-smooth((t-0.70f)/0.30f));
	return {t,glow,t >= 0.12f && t < 0.52f ? std::sin((t-0.12f)/0.40f*3.14159265f) : 0.0f,
		smooth(t/0.10f)*(1-smooth((t-0.40f)/0.24f)),
		1-outgoing,transition,-6*outgoing,6*(1-transition),false};
}
// Pure finite entrance, independent of OBS, Twitch, Qt, clocks and scheduling.
inline MotionSample sampleMotion(Motion mode, double elapsedMs, double durationMs) noexcept
{
	if (mode == Motion::None || durationMs <= 0 || elapsedMs >= durationMs) return {};
	const auto t = float(std::clamp(elapsedMs / durationMs, 0.0, 1.0));
	const auto eased = t * t * (3 - 2 * t);
	return {eased, mode == Motion::SlideFade ? 8 * (1 - eased) : 0, false};
}
using MotionClock = std::chrono::steady_clock;
// Overflow only. Pause at each end, then ease through the full strip and back.
// Pure arithmetic; no timer, allocation, layout or texture creation per frame.
inline float samplePingPong(double elapsedMs, double distance) noexcept
{
	if(!std::isfinite(elapsedMs) || !std::isfinite(distance) || distance<=0 || distance>2048)return 0;
	const double pause=1200,travel=std::max(800.0,distance/30.0*1000.0);
	const double phase=std::fmod(std::max(0.0,elapsedMs),2*(pause+travel));
	if(phase<pause)return 0;
	if(phase<pause+travel)return float(distance)*smooth(float((phase-pause)/travel));
	if(phase<2*pause+travel)return float(distance);
	return float(distance)*(1-smooth(float((phase-2*pause-travel)/travel)));
}
}
