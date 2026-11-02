// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "visual/visual-engine.hpp"
#include <graphics/graphics.h>
#include <list>
#include <functional>
#include "visual/motion.hpp"

namespace oto {
struct GpuStats { size_t entries = 0, bytes = 0; uint64_t uploads = 0, hits = 0, draws = 0, evictions = 0; };
struct DrawParams { float x = 0, y = 0, opacity = 1, offsetY = 0, offsetX = 0, viewportWidth = 0; QRectF clip; float scale = 1, angle = 0; float material=0,time=0,dissolve=0,materialVariant=0; QRectF nativeArt{0,0,1,1},nativePortrait{0,0,1,1}; QRect sourceRect;bool cellFlipX=false,cellFlipY=false;float cellAngle=0,cellPadding=1,cellShiftX=0,cellShiftY=0,cellHue=0;bool cellParty=false,cellCursed=false;float cellExtentX=1,cellExtentY=1,cellAspect=1,cellScaleX=1,cellScaleY=1,cellSlide=0;int cellFilter=0;QRectF cellLayer{0,0,1,1};bool legacyAlpha=false; };
struct ChangeLook { int pattern=0, particleShape=0, radius=5; };
struct GroupStats { size_t entries=0,bytes=0;uint64_t allocations=0,renders=0,hits=0,draws=0,evictions=0; };
class ObsVisualRenderer final {
public:
	static constexpr size_t textureBudgetBytes = 64 * 1024 * 1024;
	ObsVisualRenderer() = default;
	~ObsVisualRenderer();
	ObsVisualRenderer(const ObsVisualRenderer &) = delete;
	ObsVisualRenderer &operator=(const ObsVisualRenderer &) = delete;
	// Caller must hold the OBS graphics context and provide an SDR target and
	// projection. No Qt layout/raster work here. Inactive => no upload or draw.
	void draw(const visual::FramePtr &frame, bool active = true, const DrawParams &params = {});
	// Generic authored solid quad with original source-alpha composition; no texture/cache.
	void drawSolidRect(const QRectF &box,const QColor &color,bool active=true);
	// Encoded-sRGB premultiplied groups. Invoke descendant preparation inside
	// paint, so the active parent's target is pinned during nested composition.
	// No raster/layout/I/O/readback. Caller owns stable identities and revisions.
	void composeGroup(uint64_t identity,QSize size,uint64_t revision,const std::function<void()> &paint,bool active=true);
	void drawGroup(uint64_t identity,float x=0,float y=0,float scale=1,float opacity=1,bool active=true);
	// Final SDR output of an encoded-premultiplied group, matching Browser.
	// Never use inside another encoded group; existing drawGroup is unchanged.
	void drawGroupSrgb(uint64_t identity,float x=0,float y=0,float scale=1,float opacity=1,bool active=true);
	GroupStats groupStats() const;
	void drawAlertEnergy(float seconds, const QColor &color, float portrait, float burst, float effects, float exit, int variant, int particles, int pattern=0, float title=0, float offsetX=0, bool glassOnly=false);
	void drawHypeEnergy(QSize size,const QColor &primary,const QColor &secondary,float seconds,float phase,float burst,float bar,float ratio,int pattern,int particles,float gather=0,float morph=0,float leave=0,QRect barBounds={},QRect titleBounds={},bool authored=false,QRect levelBounds={},float levelProgress=-1,float levelFlash=0);
	void drawChange(const QRect &card, const QColor &color, const visual::ChangeSample &sample,
		int glowPercent, int particles, bool sweep, int horizontalMargin = 2, int verticalMargin = 12, ChangeLook look = {}, const QRectF &clip = {});
	// Development probe only: locks graphics, draws to a private target, reads
	// back once. Never use readback in a future source's video_render callback.
	QImage renderOffscreen(const visual::FramePtr &frame, float opacity = 1, float offsetY = 0);
	QImage renderOffscreen(QSize size, const std::function<void()> &drawScene);
	GpuStats stats() const;
	void clear() noexcept;
private:
	using Texture = std::unique_ptr<gs_texture_t, decltype(&gs_texture_destroy)>;
	struct Entry { visual::FramePtr frame; Texture texture; size_t bytes; };
	std::list<Entry> cache_;
	struct TargetDestroy { void operator()(gs_texrender_t *target) const noexcept {gs_texrender_destroy(target);} };
	struct GroupEntry {uint64_t identity,revision;QSize size;std::unique_ptr<gs_texrender_t,TargetDestroy> target;size_t bytes;bool valid=false,busy=false;};
	std::list<GroupEntry> groups_;
	GroupStats groupStats_;
	unsigned groupDepth_=0;
	void drawGroupImpl(uint64_t identity,float x,float y,float scale,float opacity,bool active,bool srgbOutput);
	void makeSpace(size_t bytes,const GroupEntry *keep=nullptr);
	gs_effect_t *groupEffect_=nullptr;
	GpuStats stats_;
	gs_effect_t *motionEffect_ = nullptr; // Single shared GPU program, graphics context only.
	gs_effect_t *changeEffect_ = nullptr;
	gs_effect_t *alertEffect_ = nullptr;
	gs_effect_t *hypeEffect_ = nullptr;
};
} // namespace oto
