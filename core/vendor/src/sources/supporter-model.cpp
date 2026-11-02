// SPDX-License-Identifier: GPL-2.0-or-later
#include "supporter-model.hpp"
#include <algorithm>
#include <stdexcept>
#include <QFontMetricsF>
#include <cmath>
namespace oto::sources {
BarLabelLayout supporterLabelLayout(const BarSettings &settings)
{
	const auto &style=settings.style;
	const int cardHeight=settings.height*style.cardHeight/100;
	const int normalHeight=style.cards?std::max(10,cardHeight*38/100):22;
	const int normalSize=style.cards?std::max(8,std::min(style.titleSize,normalHeight-2)):style.titleSize;
	QFont font(settings.labelFamily.isEmpty()?style.family:settings.labelFamily);font.setBold(style.cards);
	if(!settings.labelPixelSize){
		int pixels=normalSize;font.setPixelSize(pixels);
		while(pixels>8 && std::ceil(QFontMetricsF(font).height())>normalHeight-supporterLabelTopInset)font.setPixelSize(--pixels);
		return {pixels,normalHeight};
	}
	QFont valueFont(style.family);valueFont.setBold(style.cards);
	valueFont.setPixelSize(style.cards?std::max(12,std::min(style.valueSize,cardHeight-normalHeight-2)):style.valueSize);
	const int available=style.cards?cardHeight:settings.height-20;
	const int maxHeight=std::max(normalHeight,available-int(std::ceil(QFontMetricsF(valueFont).height()))-2);
	int low=8,high=std::clamp(settings.labelPixelSize,8,128),pixels=8;
	while(low<=high){const int middle=low+(high-low)/2;font.setPixelSize(middle);
		if(std::ceil(QFontMetricsF(font).height())+2+supporterLabelTopInset<=maxHeight){pixels=middle;low=middle+1;}else high=middle-1;}
	font.setPixelSize(pixels);
	return {pixels,std::min(maxHeight,std::max(normalHeight,int(std::ceil(QFontMetricsF(font).height()))+2+supporterLabelTopInset))};
}
int supporterValuePixelSize(const BarSettings &settings,int height,bool name)
{
	const auto &style=settings.style;
	const int inherited=style.cards?std::max(12,std::min(style.valueSize,height-2)):style.valueSize;
	if(!name || !settings.valuePixelSize)return inherited;
	QFont font(settings.valueFamily.isEmpty()?style.family:settings.valueFamily);font.setBold(style.cards);
	int low=8,high=std::clamp(settings.valuePixelSize,8,128),pixels=8;
	while(low<=high){const int middle=low+(high-low)/2;font.setPixelSize(middle);
		if(std::ceil(QFontMetricsF(font).height())+2<=height){pixels=middle;low=middle+1;}else high=middle-1;}
	return pixels;
}
BarContext supporterContext(const state::SnapshotPtr &snapshot,const std::string &previewChannel,const state::ChannelKey &selected)
{
	BarContext context;context.preview=!previewChannel.empty() || selected.origin==events::EventOrigin::Preview;context.channel=!previewChannel.empty()?state::ChannelKey{events::EventOrigin::Preview,previewChannel}:selected;
	if(!snapshot)return context;context.generation=snapshot->generation;
	const auto found=snapshot->channels.find(context.channel);if(found==snapshot->channels.end())return context;
	for(std::size_t i=0;i<4;++i){context.latestRevision[i]=found->second->latest[i].revision;context.delivered[i]=found->second->delivered[i];}
	return context;
}
std::array<QString,6> supporterValues(const state::SnapshotPtr &snapshot, int today, const BarLabels &labels, const std::string &previewChannel, const state::ChannelKey &selected)
{
	std::array<QString,6> values{labels.empty,labels.empty,labels.empty,labels.empty,QStringLiteral("0"),QStringLiteral("0")};
	if (!snapshot) return values;
	const auto found = snapshot->channels.find(previewChannel.empty() ?
		selected :
		state::ChannelKey{events::EventOrigin::Preview,previewChannel});
	if (found == snapshot->channels.end()) return values;
	const auto &channel = *found->second;
	for (size_t i = 0; i < 4; ++i) {
		const auto &last = channel.latest[i];
		if (!last.event) continue;
		values[i] = last.displayName ? QString::fromUtf8(last.displayName->c_str()) : labels.anonymous;
		if (i == 2) values[i] += QStringLiteral(" · %1").arg(qulonglong(std::get<events::CheerEvent>(last.event->data().payload).bits));
		if (i == 3) values[i] += QStringLiteral(" ×%1").arg(qulonglong(std::get<events::GiftEvent>(last.event->data().payload).count));
	}
	if (!previewChannel.empty() || channel.daily.day == today) {
		values[4] = QString::number(qulonglong(channel.daily.bits)); values[5] = QString::number(qulonglong(channel.daily.subs));
	}
	return values;
}
visual::Scene supporterScene(const BarSettings &settings, const BarLabels &labels, const std::array<QString,6> &values)
{
	if (settings.width < 600 || settings.width > 1920 || settings.height < 80 || settings.height > 240) throw std::invalid_argument("Invalid bar dimensions");
	visual::Scene scene{QSize(settings.width,settings.height), {}};
	const auto count = int(std::count(settings.fields.begin(), settings.fields.end(), true));
	if (!count) return scene;
	const auto &style=settings.style;
	const auto labelLayout=supporterLabelLayout(settings);
	const int padding = style.padding, gap = style.gap, cell = (settings.width - padding * 2 - gap * (count - 1)) / count;
	scene.nodes.push_back(visual::RectangleNode{QRect(0,0,settings.width,settings.height), style.background});
	int column = 0;
	for (size_t i = 0; i < 6; ++i) if (settings.fields[i]) {
		const int x = padding + column++ * (cell + gap);
		visual::TextNode title; title.box = QRect(x,12+supporterLabelTopInset,cell,labelLayout.height-supporterLabelTopInset); title.text = labels.titles[i]; title.pixelSize = labelLayout.pixelSize;
		title.minimumPixelSize=std::min(12,title.pixelSize);
		title.family=settings.labelFamily.isEmpty()?style.family:settings.labelFamily;title.color = style.title;
		title.overflow=settings.labelPixelSize?visual::Overflow::Ellipsis:visual::Overflow::AutoFit;
		if(!settings.labelPixelSize)title.minimumPixelSize=std::min(8,title.pixelSize);
		scene.nodes.push_back(title);
		const int valueY=settings.labelPixelSize?12+labelLayout.height+2:36;
		visual::TextNode text; text.box = QRect(x,valueY,cell,settings.height-valueY-8); text.text = values[i]; text.pixelSize = supporterValuePixelSize(settings,text.box.height(),i<4);
		text.minimumPixelSize=std::min(12,text.pixelSize);
		text.family=i<4 && !settings.valueFamily.isEmpty()?settings.valueFamily:style.family;text.color=style.value;
		text.overflow = visual::Overflow::Ellipsis; text.vertical = visual::Vertical::Center;
		scene.nodes.push_back(text);
	}
	return scene;
}
}
