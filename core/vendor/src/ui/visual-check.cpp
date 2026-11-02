// SPDX-License-Identifier: GPL-2.0-or-later
#include "visual-check.hpp"
#include "core/plugin-core.hpp"
#include "adapters/obs-visual-renderer.hpp"
#include "adapters/supporter-draw.hpp"
#include "adapters/event-list-draw.hpp"
#include "adapters/alert-draw.hpp"
#include "adapters/shoutout-draw.hpp"
#include "events/simulation.hpp"
#include "visual/asset-manager.hpp"
#include "presets/preset-registry.hpp"
#include <obs-module.h>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <QDir>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace oto::ui {
namespace {
visual::Scene checkScene()
{
	using namespace visual;
	Scene scene{QSize(640,360), {}};
	TextNode title; title.box = QRect(16,8,580,42); title.text = QString::fromUtf8(u8"Texto · José · 日本語 · مرحبا · 👩‍💻"); title.pixelSize = 24;
	title.bold = true;
	scene.nodes.push_back(title);
	const char *labels[] = {"Clip", "Ellipsis", "Auto-fit", "Wrap"};
	const Overflow modes[] = {Overflow::Clip, Overflow::Ellipsis, Overflow::AutoFit, Overflow::Wrap};
	for (int i = 0; i < 4; ++i) {
		const int top = 60 + i * 72;
		scene.nodes.push_back(CardNode{QRect(140,top,480,64), QColor(32,40,54,220),QColor(80,180,220,170),8});
		TextNode label; label.box = QRect(16,top,120,60); label.text = QString::fromUtf8(labels[i]); label.pixelSize = 18; label.vertical = Vertical::Center;
		scene.nodes.push_back(label);
		TextNode text; text.box = QRect(150,top+2,460,60); text.pixelSize = modes[i] == Overflow::Wrap ? 24 : 28; text.minimumPixelSize = 12;
		text.text = QStringLiteral("Un nombre de ejemplo bastante largo para este espacio"); text.overflow = modes[i]; text.vertical = Vertical::Center;
		scene.nodes.push_back(text);
	}
	scene.nodes.push_back(RectangleNode{QRect(620,0,20,20), QColor(255,0,0,128)});
	// Generic immutable image probe: same compositing path used by preset PNGs.
	QImage pixels(2,2,QImage::Format_RGBA8888_Premultiplied);pixels.fill(Qt::transparent);
	pixels.setPixelColor(0,0,QColor(30,220,255,180));pixels.setPixelColor(1,0,QColor(255,40,160,110));
	pixels.setPixelColor(0,1,QColor(255,220,40,255));
	scene.nodes.push_back(ImageNode{QRect(620,24,20,20),std::make_shared<const ImageData>(ImageData{pixels})});
	return scene;
}
int maximumDifference(const QImage &a, const QImage &b)
{
	if (a.size() != b.size() || a.format() != b.format()) return 256;
	int delta = 0;
	for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width() * 4; ++x)
		delta = std::max(delta, std::abs(int(a.constScanLine(y)[x]) - int(b.constScanLine(y)[x])));
	return delta;
}
QImage checkStyleChange(const std::shared_ptr<PluginCore> &core, visual::VisualEngine &engine, ObsVisualRenderer &renderer,
	const presets::Registry &registry,visual::AssetManager &assets,const QString &presetId)
{
	using namespace sources;
	BarSettings settings;settings.width=1200;settings.height=120;settings.motion=visual::Motion::StyleChange;settings.durationMs=2000;settings.presetId=presetId;settings.style=registry.resolve(presetId).style;
	BarLabels labels{{"LAST FOLLOW","LAST SUB","LAST CHEER","LAST GIFTED","DAILY BITS","DAILY SUBS"},"—","Anonymous"};
	std::array<QString,6> values{"MarinaDJ","TechnoAlex","Pepito · 500","Carlos · 5","4,850","27"};
	const auto background=assets.load(settings.style.backgroundImage),icons=assets.load(settings.style.iconImage);
	const auto now=visual::MotionClock::now();
	const auto before=prepareSupporterFrame(engine,settings,labels,values,{},now,background,icons);
	values[0]="CarlosDJ";
	const auto after=prepareSupporterFrame(engine,settings,labels,values,before,now,background,icons);
	auto render=[&](const BarFramePtr &frame,int ms,bool settled=false){return renderer.renderOffscreen(QSize(settings.width,settings.height),[&]{drawSupporterFrame(renderer,*frame,now+std::chrono::milliseconds(ms),settled);});};
	const auto original=render(before,0),updated=render(after,2000),midpoint=render(after,600);
	if(maximumDifference(original,render(after,0))>1 || maximumDifference(updated,render(after,0,true))>1 || maximumDifference(original,midpoint)<10)
		throw std::runtime_error("Change start/end/visible effect failed");
	const auto safeUploads=renderer.stats().uploads,cpuRenders=engine.stats().renders,cpuHits=engine.stats().hits;
	const auto card=after->layers[0].card;
	const int marginX=std::min(6,settings.style.gap/2),marginY=std::clamp(std::min(card.y(),settings.height-card.y()-card.height()),0,24);
	const auto box=card.adjusted(-marginX,-marginY,marginX,marginY);
	QImage gallery(1200,5*152,QImage::Format_RGBA8888_Premultiplied);gallery.fill(QColor(18,20,29));
	const char *captions[]={"1. REPOSO · MarinaDJ","2. ENTRADA · destello y partículas","3. TRANSICIÓN · CarlosDJ / barrido","4. ACTUALIZADO · glow residual","5. REPOSO · CarlosDJ"};
	const int times[]={0,240,600,1300,2000};
	{
		QPainter painter(&gallery);painter.setPen(Qt::white);
		for(int row=0;row<5;++row){const auto image=render(after,times[row]);painter.drawText(12,row*152+20,QString::fromUtf8(captions[row]));painter.drawImage(0,row*152+30,image);}
	}
	// Manual development output only. No real events or counters enter this fixture.
	const auto relative=QStringLiteral("validation/animation-0.11.4/%1").arg(presetId).toUtf8();
	char *config=obs_module_config_path(relative.constData());if(!config)throw std::runtime_error("Probe output path unavailable");
	const auto directory=QString::fromUtf8(config);bfree(config);if(!QDir().mkpath(directory))throw std::runtime_error("Probe output directory unavailable");
	if(!gallery.save(directory+"/storyboard.png"))throw std::runtime_error("Probe gallery save failed");
	for(int step=0;step<=40;++step){
		const auto image=render(after,step*50);
		for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(!box.contains(x,y))for(int channel=0;channel<4;++channel)
			if(image.constScanLine(y)[x*4+channel]!=updated.constScanLine(y)[x*4+channel])throw std::runtime_error("Change effect escaped its card");
		if(!image.save(directory+QStringLiteral("/frame-%1.png").arg(step,3,10,QChar('0'))))throw std::runtime_error("Probe frame save failed");
	}
	if(renderer.stats().uploads!=safeUploads || engine.stats().renders!=cpuRenders || engine.stats().hits!=cpuHits)
		throw std::runtime_error("Change animation rebuilt/uploaded a texture per frame");
	core->log(LogLevel::Info,QStringLiteral("Style change GPU check passed [%1]: five stages; 41 frames; other cards identical; exact final state; extra rasterizations/uploads 0; bounded 12 particles").arg(presetId).toStdString());
	return gallery;
}
QImage checkPingPong(const std::shared_ptr<PluginCore> &core, visual::VisualEngine &engine, ObsVisualRenderer &renderer,
	const presets::Definition &preset,visual::AssetManager &assets)
{
	using namespace sources;
	BarSettings settings;settings.width=1200;settings.height=120;settings.motion=visual::Motion::None;settings.presetId=preset.id;settings.style=preset.style;
	BarLabels labels{{"LAST FOLLOW","LAST SUB","LAST CHEER","LAST GIFTED","DAILY BITS","DAILY SUBS"},"—","Anonymous"};
	std::array<QString,6> values{QString::fromUtf8(u8"José_Community_Supporter_With_A_Long_Name"),"TechnoAlex","Pepito · 500","Carlos · 5","4,850","27"};
	const auto now=visual::MotionClock::now();
	const auto frame=prepareSupporterFrame(engine,settings,labels,values,{},now,assets.load(settings.style.backgroundImage),assets.load(settings.style.iconImage));
	const auto &layer=frame->layers[0];if(!frame->hasScrolling || !layer.scrollDistance)throw std::runtime_error("Long name failed to scroll");
	const double travel=std::max(800.0,layer.scrollDistance/30.0*1000.0),cycle=2400+travel*2;
	auto render=[&](double ms){return renderer.renderOffscreen(QSize(settings.width,settings.height),[&]{drawSupporterFrame(renderer,*frame,now+std::chrono::milliseconds(qRound64(ms)),true);});};
	const auto first=render(0),tail=render(1200+travel),returned=render(cycle);
	if(maximumDifference(first,tail)<10 || maximumDifference(first,returned)>1)throw std::runtime_error("Ping-pong endpoints/return failed");
	// Check the texture viewport against CPU crops at exact integer offsets.
	for(const int offset:{0,layer.scrollDistance/2,layer.scrollDistance}) {
		const auto gpu=renderer.renderOffscreen(QSize(layer.viewportWidth,layer.frame->image.height()),[&]{renderer.draw(layer.frame,true,{0,0,1,0,float(offset),float(layer.viewportWidth)});});
		if(maximumDifference(gpu,layer.frame->image.copy(offset,0,layer.viewportWidth,layer.frame->image.height()))>1)throw std::runtime_error("Horizontal GPU viewport differs from text crop");
	}
	const auto uploads=renderer.stats().uploads,renders=engine.stats().renders,hits=engine.stats().hits;
	const QRect viewport(layer.x,layer.y,layer.viewportWidth,layer.frame->image.height());
	QImage gallery(1200,5*152,QImage::Format_RGBA8888_Premultiplied);gallery.fill(QColor(18,20,29));
	const double times[]={0,1200+travel*.5,1200+travel,2400+travel*1.5,cycle};
	const char *captions[]={"1. INICIO · pausa","2. IDA · hacia la izquierda","3. FINAL · pausa","4. VUELTA · hacia la derecha","5. INICIO · pausa"};
	{QPainter painter(&gallery);painter.setPen(Qt::white);for(int i=0;i<5;++i){painter.drawText(12,i*152+20,QString::fromUtf8(captions[i]));painter.drawImage(0,i*152+30,render(times[i]));}}
	QString directory;
	if(preset.id=="neon-dj") {
		char *path=obs_module_config_path("validation/ping-pong-0.11.4");if(!path)throw std::runtime_error("Ping-pong output path unavailable");directory=QString::fromUtf8(path);bfree(path);
		if(!QDir().mkpath(directory) || !gallery.save(directory+"/storyboard.png"))throw std::runtime_error("Ping-pong gallery save failed");
	}
	const int steps=directory.isEmpty()?12:60;
	for(int i=0;i<=steps;++i) {
		const auto image=render(cycle*i/steps);
		for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(!viewport.contains(x,y))for(int c=0;c<4;++c)
			if(image.constScanLine(y)[x*4+c]!=first.constScanLine(y)[x*4+c])throw std::runtime_error("Scrolling escaped the name viewport");
		if(!directory.isEmpty() && !image.save(directory+QStringLiteral("/frame-%1.png").arg(i,3,10,QChar('0'))))throw std::runtime_error("Ping-pong frame save failed");
	}
	if(renderer.stats().uploads!=uploads || engine.stats().renders!=renders || engine.stats().hits!=hits)throw std::runtime_error("Ping-pong rebuilt/uploaded text per frame");
	core->log(LogLevel::Info,QStringLiteral("Ping-pong GPU check passed [%1]: endpoints/return; clipped name only; CPU crop delta <=1; extra rasterizations/uploads 0; cycle %2 ms").arg(preset.id).arg(qRound64(cycle)).toStdString());
	return gallery;
}
QImage checkEventList(const std::shared_ptr<PluginCore> &core,visual::VisualEngine &engine,ObsVisualRenderer &renderer)
{
	using namespace sources;
	events::EventEngine events;events.start();const auto systemNow=std::chrono::system_clock::now();const auto now=visual::MotionClock::now();
	ListSettings settings;
	char *presetPath=obs_module_file("presets/registry.json");if(!presetPath)throw std::runtime_error("Event List probe preset unavailable");const auto presetFile=QString::fromUtf8(presetPath);bfree(presetPath);
	const auto registry=presets::Registry::fromFile(presetFile);if(!registry.resolve("minimal").eventListStyle)throw std::runtime_error("Event List probe layout unavailable");settings.style=*registry.resolve("minimal").eventListStyle;settings.family=settings.style.family;
	ListLabels labels{"Anonymous","%1 Bits","%1 Subs","%1 viewers","Sub ×1","%1 months"};
	ListView sample;sample.generation=1;
	const events::EventType types[]={events::EventType::Cheer,events::EventType::Follow,events::EventType::Sub,events::EventType::Gift,events::EventType::Gift,events::EventType::Raid,events::EventType::Sub,events::EventType::Follow,events::EventType::Cheer};
	const char *names[]={"dja100","electro_life","ceyhunns","nwoand","itsjavierm","techhouse_tv","marinadj","underground_beats","previous_viewer"};
	const char *badges[]={"BITS","FOLLOW","SUB","GIFTED","GIFTED","RAID","SUB","FOLLOW","BITS"};
	const char *details[]={"500 Bits","","Sub x1","Sub x1","10 Subs","120 viewers","Sub x3","","200 Bits"};
	for(int i=0;i<9;++i)sample.items.push_back({events.process(events::makeSimulation(types[i],10+i,systemNow-std::chrono::seconds(i))).event,names[i],badges[i],details[i]});
	const char *modes[]={"appearance","left","right","top"};QImage result;
	visual::AssetManager assets(QFileInfo(presetFile).dir().filePath("assets"));
	for(const auto &definition:registry.entries())if(definition.eventListStyle) {
	settings.presetId=definition.id;settings.style=*definition.eventListStyle;settings.family=settings.style.family;
	const auto artwork=settings.style.backgroundImage.isEmpty()?visual::ImagePtr{}:assets.load(settings.style.backgroundImage);
	for(int mode=0;mode<4;++mode) {
	settings.entrance=static_cast<ListEntrance>(mode);auto view=sample;view.items.erase(view.items.begin());
	const auto before=prepareEventListFrame(engine,settings,labels,view,{},now,false,artwork);
	view=sample;view.items.resize(settings.rows);
	const auto after=prepareEventListFrame(engine,settings,labels,view,before,now,false,artwork);
	if(after->rows.size()!=8 || after->exiting.size()!=1 || after->exiting[0].base!=before->rows.back().base)throw std::runtime_error("Full list did not retain the bottom exit card");
	// A larger target catches pixels leaking outside the source even when transformed.
	auto wide=[&](int ms,bool settled=false){return renderer.renderOffscreen(QSize(settings.width+80,settings.height()+80),[&]{
		struct Offset {Offset(){gs_matrix_push();gs_matrix_translate3f(40,40,0);}~Offset(){gs_matrix_pop();}} offset;
		drawEventListFrame(renderer,*after,now+std::chrono::milliseconds(ms),settled);
	});};
	auto render=[&](int ms,bool settled=false){return wide(ms,settled).copy(40,40,settings.width,settings.height());};
	const auto original=renderer.renderOffscreen(QSize(settings.width,settings.height()),[&]{drawEventListFrame(renderer,*before,now,true);});
	const auto updated=render(6000),active=render(750);
	QImage expected(updated.size(),QImage::Format_RGBA8888_Premultiplied);expected.fill(Qt::transparent);
	{QPainter painter(&expected);for(const auto &row:after->rows){painter.drawImage(0,int(row.y),row.base->image);painter.drawImage(QRect(row.nameX,int(row.y),row.nameWidth,settings.rowHeight),row.name->image,QRect(0,0,row.nameWidth,settings.rowHeight));}}
	const auto startDelta=maximumDifference(original,render(0)),cpuDelta=maximumDifference(expected,updated),endDelta=maximumDifference(updated,render(6000,true)),activeDelta=maximumDifference(active,updated);
	core->log(LogLevel::Info,QStringLiteral("Event List GPU probe [%1/%2] deltas: start %3; CPU %4; final %5; active %6").arg(definition.id).arg(modes[mode]).arg(startDelta).arg(cpuDelta).arg(endDelta).arg(activeDelta).toStdString());
	if(startDelta>1 || cpuDelta>1 || endDelta>1 || activeDelta<10)throw std::runtime_error("Event List start/final/composition/visible entrance failed");
	const auto middle=render(250);auto withoutExit=*after;withoutExit.exiting.clear();
	const auto missingBottom=renderer.renderOffscreen(QSize(settings.width,settings.height()),[&]{drawEventListFrame(renderer,withoutExit,now+std::chrono::milliseconds(250));});
	if(maximumDifference(middle,missingBottom)<10)throw std::runtime_error("Bottom exit was not visible halfway through shift");
	const auto completedShift=renderer.renderOffscreen(QSize(settings.width,settings.height()),[&]{drawEventListFrame(renderer,withoutExit,now+std::chrono::milliseconds(500));});
	if(maximumDifference(render(500),completedShift)>1)throw std::runtime_error("Bottom exit remains after 500 ms");
	QImage clippedExpected(300,100,QImage::Format_RGBA8888_Premultiplied);clippedExpected.fill(Qt::transparent);
	const QRectF clip(5,7,197,31);
	{QPainter painter(&clippedExpected);painter.setClipRect(clip);painter.drawImage(-17,-9,after->rows[0].base->image);}
	const auto clipped=renderer.renderOffscreen(clippedExpected.size(),[&]{renderer.draw(after->rows[0].base,true,{-17,-9,1,0,0,0,clip});});
	if(maximumDifference(clipped,clippedExpected)>1)throw std::runtime_error("Source clip differs from CPU composition");
	const auto uploads=renderer.stats().uploads,renders=engine.stats().renders;
	const auto relative=QStringLiteral("validation/event-list-" OTO_VERSION "/%1/%2").arg(definition.id).arg(modes[mode]).toUtf8();char *path=obs_module_config_path(relative.constData());if(!path)throw std::runtime_error("Event List probe path unavailable");const auto directory=QString::fromUtf8(path);bfree(path);if(!QDir().mkpath(directory))throw std::runtime_error("Event List probe directory unavailable");
	const QRect sourceBounds(40,40,settings.width,settings.height());
	for(int step=0;step<=40;++step){
		const auto padded=wide(step*150);for(int y=0;y<padded.height();++y)for(int x=0;x<padded.width();++x)if(!sourceBounds.contains(x,y) && padded.pixelColor(x,y).alpha())throw std::runtime_error("Event List entrance escaped source clip");
		const auto image=padded.copy(sourceBounds);
		if(!image.save(directory+QStringLiteral("/frame-%1.png").arg(step,3,10,QChar('0'))))throw std::runtime_error("Event List probe export failed");
	}
	if(renderer.stats().uploads!=uploads || engine.stats().renders!=renders)throw std::runtime_error("Event List animation rebuilt/uploaded per frame");
	if(!updated.save(directory+"/minimal.png"))throw std::runtime_error("Event List final export failed");
	core->log(LogLevel::Info,QStringLiteral("Event List GPU check passed [%1/%2]: 41 full-list frames; eight rows shift/bottom exit; CPU composition/clip delta <=1; exact quiet final; transformed source bounds transparent; extra rasterizations/uploads 0").arg(definition.id).arg(modes[mode]).toStdString());result=updated;
	for(int duration:{1000,12000}) {
		auto timing=settings;timing.durationMs=duration;
		auto prior=sample;prior.items.erase(prior.items.begin());
		const auto timedBefore=prepareEventListFrame(engine,timing,labels,prior,{},now,false,artwork);
		auto incoming=sample;incoming.items.resize(timing.rows);
		const auto timedAfter=prepareEventListFrame(engine,timing,labels,incoming,timedBefore,now,false,artwork);
		const auto timedUploads=renderer.stats().uploads,timedRenders=engine.stats().renders;
		// Match wide()'s target/projection/translation as well as its timeline.
		// Procedural step boundaries can round differently with a different viewport.
		auto timedRender=[&](double ms){return renderer.renderOffscreen(QSize(timing.width+80,timing.height()+80),[&]{
			struct Offset {Offset(){gs_matrix_push();gs_matrix_translate3f(40,40,0);}~Offset(){gs_matrix_pop();}} offset;
			drawEventListFrame(renderer,*timedAfter,now+std::chrono::duration_cast<visual::MotionClock::duration>(std::chrono::microseconds(std::llround(ms*1000))));
		}).copy(40,40,timing.width,timing.height());};
		// D/16 is representable in the clock at both limits; D/24 rounds to a
		// different subpixel at 1000 ms and can cross a point-sampling boundary.
		const auto timedStart=maximumDifference(timedRender(0),original),timedMiddle=maximumDifference(timedRender(duration/16.0),render(375)),timedActive=maximumDifference(timedRender(duration/8.0),active),timedFinal=maximumDifference(timedRender(duration),updated);
		core->log(LogLevel::Info,QStringLiteral("Event List duration GPU probe [%1/%2/%3 ms] deltas: start %4; shift %5; active %6; final %7").arg(definition.id).arg(modes[mode]).arg(duration).arg(timedStart).arg(timedMiddle).arg(timedActive).arg(timedFinal).toStdString());
		if(timedStart>1 || timedMiddle>1 || timedActive>1 || timedFinal>1)throw std::runtime_error("Selected Event List duration changes choreography or leaves an unsettled row");
		if(renderer.stats().uploads!=timedUploads || engine.stats().renders!=timedRenders)throw std::runtime_error("Duration animation rebuilt/uploaded per frame");
		core->log(LogLevel::Info,QStringLiteral("Event List duration GPU check passed [%1/%2/%3 ms]: scaled entrance/shift/exit/effects; exact final; extra rasterizations/uploads 0").arg(definition.id).arg(modes[mode]).arg(duration).toStdString());
	}
	}
	auto finalView=sample;finalView.items.resize(settings.rows);
	const auto full=prepareEventListFrame(engine,settings,labels,finalView,{},now,false,artwork);result=renderer.renderOffscreen(QSize(settings.width,settings.height()),[&]{drawEventListFrame(renderer,*full,now,true);});
	const auto finalRelative=QStringLiteral("validation/event-list-" OTO_VERSION "/%1/list.png").arg(definition.id).toUtf8();char *finalPath=obs_module_config_path(finalRelative.constData());if(!finalPath)throw std::runtime_error("Event List gallery path unavailable");const auto finalFile=QString::fromUtf8(finalPath);bfree(finalPath);if(!result.save(finalFile))throw std::runtime_error("Event List gallery save failed");
	for(int opacity:{0,50,100}) {
		auto translucent=settings;translucent.backgroundOpacity=opacity;
		const auto faded=prepareEventListFrame(engine,translucent,labels,finalView,full,now,false,artwork);
		QImage cpu(QSize(settings.width,settings.height()),QImage::Format_RGBA8888_Premultiplied);cpu.fill(Qt::transparent);
		{QPainter painter(&cpu);for(const auto &row:faded->rows){if(row.entered!=visual::MotionClock::time_point{})throw std::runtime_error("Opacity replayed an event");painter.drawImage(0,int(row.y),row.base->image);painter.drawImage(QRect(row.nameX,int(row.y),row.nameWidth,settings.rowHeight),row.name->image,QRect(0,0,row.nameWidth,settings.rowHeight));}}
		auto renderFaded=[&]{return renderer.renderOffscreen(cpu.size(),[&]{drawEventListFrame(renderer,*faded,now,true);});};
		const auto gpu=renderFaded();if(maximumDifference(cpu,gpu)>1 || (opacity<100 && maximumDifference(gpu,result)<10) || (opacity==100 && maximumDifference(gpu,result)>1))throw std::runtime_error("Opacity GPU composition/restoration failed");
		const auto uploads=renderer.stats().uploads,renders=engine.stats().renders;renderFaded();renderFaded();
		if(renderer.stats().uploads!=uploads || engine.stats().renders!=renders)throw std::runtime_error("Opacity rebuilt/uploaded in idle");
		auto prior=sample;prior.items.erase(prior.items.begin());
		const auto fadedBefore=prepareEventListFrame(engine,translucent,labels,prior,{},now,false,artwork);
		const auto fadedAfter=prepareEventListFrame(engine,translucent,labels,finalView,fadedBefore,now,false,artwork);
		auto moving=[&](int ms){return renderer.renderOffscreen(cpu.size(),[&]{drawEventListFrame(renderer,*fadedAfter,now+std::chrono::milliseconds(ms));});};
		moving(0);moving(750);const auto warmedUploads=renderer.stats().uploads,warmedRenders=engine.stats().renders;
		moving(250);moving(1500);if(maximumDifference(moving(6000),gpu)>1 || renderer.stats().uploads!=warmedUploads || engine.stats().renders!=warmedRenders)throw std::runtime_error("Opacity animation rebuilt/uploaded or failed to settle");
		if(!gpu.save(QFileInfo(finalFile).dir().filePath(QStringLiteral("opacity-%1.png").arg(opacity))))throw std::runtime_error("Opacity export failed");
		core->log(LogLevel::Info,QStringLiteral("Event List opacity GPU check passed [%1/%2]: CPU delta <=1; original restored at 100; no replay; extra rasterizations/uploads 0").arg(definition.id).arg(opacity).toStdString());
	}
	}
	return result;
}
QImage checkAlerts(const std::shared_ptr<PluginCore> &core,visual::VisualEngine &engine,ObsVisualRenderer &renderer,visual::ImagePtr portrait,const events::User &actor)
{
	char *path=obs_module_file("presets/registry.json");const auto filename=QString::fromUtf8(path?path:"");bfree(path);
	const auto registry=presets::Registry::fromFile(filename);visual::AssetManager assets(QDir(QFileInfo(filename).absolutePath()).filePath("assets"));
	events::EventEngine events;events.start();state::SharedState shared;shared.start();state::TextCache text;
	constexpr events::EventType types[]={events::EventType::Follow,events::EventType::Sub,events::EventType::Gift,events::EventType::Gift,events::EventType::Raid,events::EventType::Cheer};QImage result;
	const auto now=visual::MotionClock::now();
	for(const auto &preset:registry.entries()){
	if(!preset.alertStyle)continue;const auto &style=*preset.alertStyle;
	for(int variant=0;variant<6;++variant){
		auto draft=events::makeSimulation(types[variant],variant+1,std::chrono::system_clock::now());if(variant==2 || variant==3)std::get<events::GiftEvent>(draft.payload).count=variant==3?10:1;
		if(portrait && !actor.id.empty())std::visit([&](auto &payload){using T=std::decay_t<decltype(payload)>;if constexpr(std::is_same_v<T,events::FollowEvent>)payload.follower=actor;else if constexpr(std::is_same_v<T,events::SubEvent>)payload.subscriber=actor;else if constexpr(std::is_same_v<T,events::GiftEvent>)payload.gifter=actor;else if constexpr(std::is_same_v<T,events::RaidEvent>)payload.raider=actor;else if constexpr(std::is_same_v<T,events::CheerEvent>)payload.user=actor;},draft.payload);
		const auto event=events.process(std::move(draft)).event;const auto snapshot=shared.prepare({event},text);const auto entry=snapshot->channels.begin()->second->activity.front();sources::AlertSettings settings;
		settings.presetId=preset.id;const auto &clip=style.iconAnimations[variant];
		const auto frame=sources::prepareAlertFrame(engine,settings,style,entry,clip.isEmpty()?assets.load(style.atlas):visual::ImagePtr{},portrait,now,clip.isEmpty()?visual::ImagePtr{}:assets.load(clip));
		const auto render=[&](int ms){return renderer.renderOffscreen({1000,1000},[&]{drawAlertFrame(renderer,*frame,now+std::chrono::milliseconds(ms));});};
		const auto zero=render(0),hold=render(2500),exit=render(5100),final=render(6000);
		QImage blank({1000,1000},QImage::Format_RGBA8888_Premultiplied);blank.fill(Qt::transparent);
		if(maximumDifference(zero,blank)!=0 || maximumDifference(final,blank)!=0 || maximumDifference(hold,exit)<10 || hold.pixelColor(0,0).alpha()!=0)throw std::runtime_error("Alert GPU stages/transparency failed");
		// Warm all layers once; the native export must raster/upload nothing.
		const auto uploads=renderer.stats().uploads,renders=engine.stats().renders,hits=engine.stats().hits;
		// Same visual clock on either side of the turn; avoid crossing a normal
		// discrete icon-cell boundary when checking phase continuity.
		const auto boundaryDelta=maximumDifference(render(4499),render(4501));
		core->log(LogLevel::Info,QStringLiteral("Animated Alerts boundary GPU [%1/%2]: maximum delta %3 (limit 4)").arg(preset.id).arg(variant).arg(boundaryDelta).toStdString());
		if(boundaryDelta>4)throw std::runtime_error("Alert hold/exit boundary jumps pixels");
		const auto relative=QStringLiteral("validation/alerts-%1/%2/%3").arg(QString::fromLatin1(OTO_VERSION),preset.id).arg(variant).toUtf8();char *directoryPath=obs_module_config_path(relative.constData());const auto directory=QString::fromUtf8(directoryPath?directoryPath:"");bfree(directoryPath);if(!QDir().mkpath(directory))throw std::runtime_error("Alert probe directory failed");
		// Fixed geometry and opacity prove that the shader changes internally,
		// rather than merely translating a prepared image.
		const auto energy=[&](float seconds){return renderer.renderOffscreen({1000,1000},[&]{renderer.drawAlertEnergy(seconds,frame->accent,1,1,.5f,0,variant,0,frame->energyPattern,1);});};
		if(maximumDifference(energy(2.5f),energy(2.8f))<30)throw std::runtime_error("Procedural energy has no internal motion");
		const auto emblem=[&](float seconds){return renderer.renderOffscreen({128,128},[&]{DrawParams p;p.sourceRect=sources::alertIconCell(seconds*1000);renderer.draw(frame->emblem,true,p);});};
		if(maximumDifference(emblem(2.5f),emblem(2.8f))<20)throw std::runtime_error("Alert icon has no internal animation");
		for(int n=0;n<=180;++n){const int ms=qRound(n*1000.0/30);const auto image=render(ms);if(!image.save(directory+QStringLiteral("/frame-%1.png").arg(n,3,10,QChar('0'))))throw std::runtime_error("Alert probe capture failed");
			if(n%3!=0)continue;const auto padded=renderer.renderOffscreen({1200,1200},[&]{gs_matrix_push();gs_matrix_translate3f(100,100,0);drawAlertFrame(renderer,*frame,now+std::chrono::milliseconds(ms));gs_matrix_pop();});
			for(int y=0;y<1200;++y)for(int x=0;x<1200;++x)if((x<100 || x>=1100 || y<100 || y>=1100) && padded.constScanLine(y)[x*4+3])throw std::runtime_error("Alert graphics exceed 1000x1000 square");
		}
		if(renderer.stats().uploads!=uploads || engine.stats().renders!=renders || engine.stats().hits!=hits)throw std::runtime_error("Alert animation rebuilt/uploaded layers");
		for(const auto timings:{sources::AlertTimes{100,0,100},sources::AlertTimes{10000,60000,10000}}){auto changed=*frame;changed.settings.times=timings;const auto exact=renderer.renderOffscreen({1000,1000},[&]{drawAlertFrame(renderer,changed,now+std::chrono::milliseconds(timings.total()));});if(maximumDifference(exact,blank)!=0)throw std::runtime_error("Alert duration bounds failed");}
		core->log(LogLevel::Info,QStringLiteral("Animated Alerts GPU check passed [%1/%2]: procedural internal motion; 1000x1000; 181 frames at 30fps; 61 transparent outside guards; exact start/end; two timing bounds; extra rasterizations/uploads 0; portrait %3").arg(preset.id).arg(variant).arg(portrait?"cached Twitch":"fallback").toStdString());if(variant==0)result=hold;
	}}return result;
}
QImage checkChange(const std::shared_ptr<PluginCore> &core, visual::VisualEngine &engine, ObsVisualRenderer &renderer)
{
	auto modulePath=[](const char *relative){char *path=obs_module_file(relative);if(!path)throw std::runtime_error("Probe asset path unavailable");const auto result=QString::fromUtf8(path);bfree(path);return result;};
	const auto registry=presets::Registry::fromFile(modulePath("presets/registry.json"));
	visual::AssetManager assets(modulePath("presets/assets"));
	QImage result;
	for(const auto &preset:registry.entries()){
		checkStyleChange(core,engine,renderer,registry,assets,preset.id);
		const auto gallery=checkPingPong(core,engine,renderer,preset,assets);if(preset.id=="neon-dj")result=gallery;
	}
	return result;
}
}
void showShoutoutCheck(QWidget *parent,const std::shared_ptr<PluginCore> &core,const std::shared_ptr<visual::VisualEngine> &engine,const std::shared_ptr<ObsVisualRenderer> &renderer,visual::ImagePtr portrait)
{
	auto modulePath=[](const char *relative){char *path=obs_module_file(relative);if(!path)throw std::runtime_error("Shoutout asset path unavailable");const auto result=QString::fromUtf8(path);bfree(path);return result;};
	const auto registry=presets::Registry::fromFile(modulePath("presets/registry.json"));visual::AssetManager assets(modulePath("presets/assets"));
	char *path=obs_module_config_path("validation/shoutout-" OTO_VERSION);const auto directory=QString::fromUtf8(path?path:"");bfree(path);if(directory.isEmpty() || !QDir().mkpath(directory))throw std::runtime_error("Shoutout validation path unavailable");
	QImage gallery(1600,2000,QImage::Format_RGBA8888_Premultiplied);gallery.fill(QColor(20,20,29));int row=0;
	struct Lock{Lock(){obs_enter_graphics();}~Lock(){obs_leave_graphics();}} lock;
	for(const auto &preset:registry.entries()){
		if(!preset.shoutoutStyle)continue;const auto &style=*preset.shoutoutStyle;
		for(bool longText:{false,true}){
			const auto key=core->createShoutoutPreview(longText,{"preview-viewer","DJ_PEPITO"},"viewer","Just Chatting",125000);
			const auto entry=core->sharedSnapshot()->channels.at({events::EventOrigin::Preview,key})->shoutouts.back();core->releaseSourcePreview(key);
			sources::ShoutoutSettings settings;settings.preset=preset.id;settings.times={2000,longText?16000:3000,1000};const auto now=visual::MotionClock::now();const auto frame=sources::prepareShoutoutFrame(*engine,settings,style,entry,portrait,now,assets.load(style.atlas));
			auto render=[&](int ms,bool padded=false){return renderer->renderOffscreen(padded?QSize(1000,1000):QSize(800,800),[&]{gs_matrix_push();if(padded)gs_matrix_translate3f(100,100,0);drawShoutoutFrame(*renderer,*frame,now+std::chrono::milliseconds(ms));gs_matrix_pop();});};
			QImage blank(800,800,QImage::Format_RGBA8888_Premultiplied);blank.fill(Qt::transparent);
			const auto hold=render(settings.times.entry+200);render(0);render(500);render(settings.times.total()-500);
			const auto uploads=renderer->stats().uploads,rasters=engine->stats().renders;
			if(maximumDifference(render(settings.times.total()),blank)!=0 || maximumDifference(hold,render(500))<5 || maximumDifference(hold,render(settings.times.total()-500))<5)throw std::runtime_error("Shoutout entrance/hold/exit failed");
			for(const auto times:{sources::AlertTimes{100,0,100},sources::AlertTimes{10000,60000,10000}}){auto changed=*frame;changed.settings.times=times;const auto final=renderer->renderOffscreen({800,800},[&]{drawShoutoutFrame(*renderer,changed,now+std::chrono::milliseconds(times.total()));});if(maximumDifference(final,blank)!=0)throw std::runtime_error("Shoutout independent times failed");}
			const auto exportDir=directory+"/"+preset.id+(longText?"/long":"/normal");if(!QDir().mkpath(exportDir))throw std::runtime_error("Shoutout export directory failed");
			for(int step=0;step<=60;++step){const int ms=qRound(settings.times.total()*step/60.0);const auto padded=render(ms,true);for(int y=0;y<1000;++y)for(int x=0;x<1000;++x)if(!QRect(100,100,800,800).contains(x,y) && padded.constScanLine(y)[x*4+3])throw std::runtime_error("Shoutout exceeded 800 square");if(!padded.copy(100,100,800,800).save(exportDir+QStringLiteral("/frame-%1.png").arg(step,3,10,QChar('0'))))throw std::runtime_error("Shoutout capture failed");}
			if(renderer->stats().uploads!=uploads || engine->stats().renders!=rasters)throw std::runtime_error("Shoutout rebuilt/uploaded during animation");
			if(!hold.save(exportDir+"/hold.png"))throw std::runtime_error("Shoutout hold export failed");
			if(!longText){QPainter painter(&gallery);painter.setPen(Qt::white);painter.drawText((row%2)*800+20,(row/2)*400+20,preset.name);painter.drawImage((row%2)*800,(row/2)*400-200,hold);++row;}
			core->log(LogLevel::Info,QStringLiteral("Shoutout GPU check passed [%1/%2]: 800x800; entry/hold/exit; adaptive name %3 px; scroll %4 px; 61 exterior guards; extra raster/uploads 0").arg(preset.id).arg(longText?"long":"normal").arg(frame->namePixelSize).arg(frame->scrollDistance).toStdString());
		}
	}
	if(!gallery.save(directory+"/gallery.png"))throw std::runtime_error("Shoutout gallery save failed");
	auto *dialog=new QDialog(parent);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(obs_module_text("Shoutout.Check"));auto *layout=new QVBoxLayout(dialog);auto *label=new QLabel(dialog);label->setPixmap(QPixmap::fromImage(gallery).scaledToHeight(850,Qt::SmoothTransformation));layout->addWidget(label);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,dialog);QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);layout->addWidget(buttons);dialog->show();
}
void showVisualCheck(QWidget *parent, const std::shared_ptr<PluginCore> &core,
	const std::shared_ptr<visual::VisualEngine> &visual, const std::shared_ptr<ObsVisualRenderer> &renderer,visual::ImagePtr portrait,events::User actor)
{
	// Single manual checkpoint; no timer or event consumption.
	const auto scene = checkScene();
	const auto frame = visual->prepare(scene);
	if (visual->prepare(scene) != frame) throw std::runtime_error("Shared visual frame was not reused");
	QImage second;
	{
	// Keep counters coherent with concurrently rendered sources during this manual probe.
	struct ProbeLock { ProbeLock() { obs_enter_graphics(); } ~ProbeLock() { obs_leave_graphics(); } } lock;
	const auto first = renderer->renderOffscreen(frame);
	const auto before = renderer->stats();
	second = renderer->renderOffscreen(frame);
	const auto after = renderer->stats();
	renderer->draw(nullptr, false);
	const auto delta = std::max(maximumDifference(frame->image, first), maximumDifference(first, second));
	if (delta > 1 || before.uploads != after.uploads || after.hits != before.hits + 1)
		throw std::runtime_error("GPU pixel or cache validation failed");
	// Fixed animation checkpoints through the real shader, without a UI timer.
	const auto motionBefore = renderer->stats();
	int motionDelta = 0;
	for (const auto opacity : {0.0f,0.5f,1.0f}) {
		const int offset = opacity == 0.5f ? 4 : 0;
		const auto actual = renderer->renderOffscreen(frame,opacity,float(offset));
		QImage expected(frame->image.size(),QImage::Format_RGBA8888_Premultiplied); expected.fill(Qt::transparent);
		for (int y=offset;y<expected.height();++y) for (int x=0;x<expected.width()*4;++x)
			expected.scanLine(y)[x] = uint8_t(std::lround(frame->image.constScanLine(y-offset)[x]*opacity));
		motionDelta = std::max(motionDelta,maximumDifference(expected,actual));
	}
	const auto motionAfter = renderer->stats();
	if (motionDelta > 1 || motionBefore.uploads != motionAfter.uploads)
		throw std::runtime_error("Motion opacity/translation or texture reuse failed");
	core->log(LogLevel::Info,QStringLiteral("Motion GPU check passed: opacity 0/0.5/1; slide 4px; max pixel delta %1; extra uploads 0")
		.arg(motionDelta).toStdString());
	core->log(LogLevel::Info, QStringLiteral("Visual check passed: 640x360; rasterizations %1; CPU hits %2; GPU uploads %3; GPU hits %4; max pixel delta %5")
		.arg(qulonglong(visual->stats().renders)).arg(qulonglong(visual->stats().hits)).arg(qulonglong(after.uploads)).arg(qulonglong(after.hits)).arg(delta).toStdString());
	second=checkChange(core,*visual,*renderer);
	second=checkEventList(core,*visual,*renderer);
	second=checkAlerts(core,*visual,*renderer,std::move(portrait),actor);
	}
	auto dialog = std::make_unique<QDialog>(parent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(QString::fromUtf8(obs_module_text("Panel.VisualCheck")));
	auto *layout = new QVBoxLayout(dialog.get());
	auto *description = new QLabel(QString::fromUtf8(obs_module_text("Panel.VisualPassed")), dialog.get());
	description->setWordWrap(true); layout->addWidget(description);
	auto *image = new QLabel(dialog.get()); image->setPixmap(QPixmap::fromImage(second).scaledToWidth(720,Qt::SmoothTransformation)); layout->addWidget(image);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog.get());
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog.get(), &QDialog::close); layout->addWidget(buttons);
	dialog->setFixedSize(dialog->sizeHint()); dialog->show(); dialog.release();
}
} // namespace oto::ui
