// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin-core.hpp"
#include "events/simulation.hpp"

#include <stdexcept>
#include <utility>
#include <limits>
#include <algorithm>

namespace oto {
PluginCore::PluginCore(std::unique_ptr<IConfigStore> config, std::unique_ptr<ILogger> logger, std::unique_ptr<IDailyStore> dailyStore)
	: config_(std::move(config)), logger_(std::move(logger)), dailyStore_(std::move(dailyStore))
{
	if (!config_ || !logger_)
		throw std::invalid_argument("PluginCore requires configuration and logging services");
}

PluginCore::~PluginCore()
{
	stop();
}

void PluginCore::start()
{
	if (running())
		return;
	const auto loaded = config_->load();
	configurationWritable_ = loaded.status == ConfigStatus::Missing || loaded.status == ConfigStatus::Loaded;
	// Never adopt partially parsed or unsupported configuration.
	const auto initial = loaded.status == ConfigStatus::Loaded ? loaded.settings : Settings{};
	state_.store(runningBit | (initial.diagnosticsEnabled ? diagnosticsBit : 0));
	if (!configurationWritable_)
		logger_->write(LogLevel::Warning, "Configuration unavailable or unsupported; defaults active, file preserved");
	else if (loaded.status == ConfigStatus::Missing && !config_->save(initial))
		logger_->write(LogLevel::Warning, "Could not save default configuration");
	eventEngine_.start();
	logger_->write(LogLevel::Info, "Event engine started");
	executionQueue_.start();
	logger_->write(LogLevel::Info, "Execution queue started");
	const auto daily=dailyStore_?dailyStore_->load():state::DailyBook{};
	state::LatestBook latest;
	if(dailyStore_)for(auto &[key,entries]:dailyStore_->loadLatest()){
		if(daily.find(key)==daily.end())throw std::runtime_error("Restored latest channel has no daily record");
		for(std::size_t i=0;i<entries.size();++i)if(entries[i]){
			if(entries[i]->origin!=key.origin || entries[i]->channelId!=key.channelId || state::slot(events::typeOf(entries[i]->payload))!=i)throw std::runtime_error("Restored latest event has invalid routing");
			auto result=eventEngine_.process(std::move(*entries[i]));if(!result)throw std::runtime_error("Restored latest event is invalid; file preserved");latest[key][i]=std::move(result.event);
		}
	}
	state::ActivityBook activity;
	if(dailyStore_)for(auto &[key,entries]:dailyStore_->loadActivity()){
		if(daily.find(key)==daily.end() || entries.size()>state::ChannelState::activityCapacity)throw std::runtime_error("Invalid restored history channel");
		auto &saved=activity[key];
		for(auto &draft:entries){
			if(draft.origin!=key.origin || draft.channelId!=key.channelId || state::slot(events::typeOf(draft.payload))>4)throw std::runtime_error("Invalid restored history routing");
			auto result=eventEngine_.process(std::move(draft));if(!result)throw std::runtime_error("Invalid restored history; file preserved");saved.push_back(std::move(result.event));
		}
	}
	sharedState_.start(daily,latest,&textCache_,activity);
	// The sole saved Twitch channel is already safe to display while reconnecting.
	const auto count=std::count_if(daily.begin(),daily.end(),[](const auto &item){return item.first.origin==events::EventOrigin::Twitch;});
	if(count==1)selectedChannel_=std::find_if(daily.begin(),daily.end(),[](const auto &item){return item.first.origin==events::EventOrigin::Twitch;})->first;
	logger_->write(LogLevel::Info, "Shared state and text cache started");
	logger_->write(LogLevel::Info, "Core started");
	log(LogLevel::Debug, "Diagnostic logging enabled");
}

void PluginCore::stop() noexcept
{
	if ((state_.exchange(0) & runningBit) == 0)
		return;
	configurationWritable_ = false;
	stateChanged_ = {};
	panelTestChannel_.reset();
	presentationQueue_.clear();
	executionQueue_.stop();
	logger_->write(LogLevel::Info, "Execution queue stopped; pending events released");
	sharedState_.stop();
	textCache_.clear();
	logger_->write(LogLevel::Info, "Shared state and text cache released");
	eventEngine_.stop();
	logger_->write(LogLevel::Info, "Event engine stopped");
	logger_->write(LogLevel::Info, "Core stopped");
}

SettingsUpdate PluginCore::setDiagnosticsEnabled(bool enabled)
{
	if (!running())
		return SettingsUpdate::NotRunning;
	if (!configurationWritable_)
		return SettingsUpdate::ReadOnly;
	if (settings().diagnosticsEnabled == enabled)
		return SettingsUpdate::Unchanged;
	if (!config_->save(Settings{enabled})) {
		logger_->write(LogLevel::Error, "Configuration save failed; active settings unchanged");
		return SettingsUpdate::SaveFailed;
	}
	state_.store(runningBit | (enabled ? diagnosticsBit : 0));
	logger_->write(LogLevel::Info, enabled ? "Diagnostic logging enabled" : "Diagnostic logging disabled");
	return SettingsUpdate::Applied;
}

bool PluginCore::configurationWritable() const noexcept
{
	return configurationWritable_;
}

SubmissionResult PluginCore::processEvent(events::EventDraft draft)
{
	if (!running()) return {nullptr, events::EventError::Stopped};
	auto result = eventEngine_.process(std::move(draft));
	log(result ? LogLevel::Debug : LogLevel::Warning, result ? "Event normalized" : "Event rejected: invalid input");
	if (!result) return {nullptr, result.error, {execution::EnqueueStatus::InvalidEvent}};
	if(result.event->data().origin==events::EventOrigin::Twitch){
		const auto snapshot=sharedState_.snapshot();const auto channel=snapshot->channels.find({events::EventOrigin::Twitch,result.event->data().channelId});
		if(channel!=snapshot->channels.end())for(const auto &entry:channel->second->activity){
			bool duplicate=entry.event->data().id==result.event->data().id;
			const auto *old=std::get_if<events::FollowEvent>(&entry.event->data().payload),*follow=std::get_if<events::FollowEvent>(&result.event->data().payload);
			if(entry.delivery==0 && old && follow && old->follower.id==follow->follower.id && std::chrono::duration_cast<std::chrono::seconds>(entry.event->data().occurredAt.time_since_epoch())==std::chrono::duration_cast<std::chrono::seconds>(result.event->data().occurredAt.time_since_epoch()))duplicate=true;
			if(duplicate)return {std::move(result.event),events::EventError::None,{execution::EnqueueStatus::Duplicate}};
		}
	}
	const auto queued = executionQueue_.enqueue(result.event);
	return {std::move(result.event), events::EventError::None, queued};
}

SubmissionResult PluginCore::simulateEvent(events::EventType type)
{
	if (!running()) return {nullptr, events::EventError::Stopped};
	if (simulationSequence_ == std::numeric_limits<std::uint64_t>::max())
		throw std::overflow_error("Simulation sequence exhausted");
	return processEvent(events::makeSimulation(type, ++simulationSequence_, std::chrono::system_clock::now()));
}
SubmissionResult PluginCore::simulatePanelEvent(events::EventType type)
{
	if(!running())return {nullptr,events::EventError::Stopped};
	const bool first=!panelTestChannel_;
	if(first){
		const auto snapshot=sharedSnapshot();
		const auto previews=std::count_if(snapshot->channels.begin(),snapshot->channels.end(),[](const auto &item){return item.first.origin==events::EventOrigin::Preview;});
		if(previews>=14 || snapshot->channels.size()>=15)throw std::length_error("Preview capacity reached");
		if(previewSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Preview IDs exhausted");
		panelTestChannel_=state::ChannelKey{events::EventOrigin::Preview,"panel-test-"+std::to_string(++previewSequence_)};
		// Consumers subscribe before prepare() captures playback ownership. Their
		// live subscription remains in place throughout this TEST.
		if(stateChanged_)stateChanged_();
	}
	try{
		if(simulationSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Simulation IDs exhausted");
		const auto now=std::chrono::system_clock::now();
		auto draft=events::makeSimulation(type,++simulationSequence_,now);
		draft.origin=events::EventOrigin::Preview;draft.channelId=panelTestChannel_->channelId;
		if(type==events::EventType::Chat)std::get<events::ChatEvent>(draft.payload).message="TEST · "+std::get<events::ChatEvent>(draft.payload).message;
		if(type==events::EventType::Shoutout)std::get<events::ShoutoutEvent>(draft.payload).target.displayName="TEST · DemoViewer";
		if(type==events::EventType::HypeTrain){
			const auto snapshot=sharedSnapshot();const auto found=snapshot->channels.find(*panelTestChannel_);
			auto &hype=std::get<events::HypeTrainEvent>(draft.payload);
			hype.stage=found!=snapshot->channels.end() && found->second->hypeTrain?events::HypeTrainStage::Progress:events::HypeTrainStage::Begin;
			hype.trainId=draft.channelId;hype.startedAt=now;hype.expiresAt=now+std::chrono::seconds(90);hype.progress=750;
			hype.contributors={{{"sim-viewer","DemoViewer"},750,3,250,500,1}};
		}
		auto result=processEvent(std::move(draft));
		if(!result){if(first)endPanelTest();return result;}
		processPendingEventsFor(events::EventOrigin::Preview,panelTestChannel_->channelId);
		log(LogLevel::Info,"Panel TEST delivered through central queue");
		return result;
	}catch(...){if(first)endPanelTest();throw;}
}
void PluginCore::endPanelTest()
{
	if(!panelTestChannel_)return;
	const auto channel=panelTestChannel_->channelId;
	cancelPendingEventsFor(events::EventOrigin::Preview,channel);
	presentationQueue_.discardPanelTest(*panelTestChannel_);
	panelTestChannel_.reset();releaseSourcePreview(channel);
	if(stateChanged_)stateChanged_();
	log(LogLevel::Info,"Panel TEST ended; live channel restored");
}
std::size_t PluginCore::restoreChannelHistory(const state::ChannelKey &channel,std::vector<events::EventDraft> drafts)
{
	if(!running() || drafts.size()>state::ChannelState::activityCapacity)throw std::invalid_argument("Invalid historical batch");
	std::vector<events::EventPtr> events;
	for(auto &draft:drafts){auto result=eventEngine_.process(std::move(draft));if(!result)throw std::runtime_error("Invalid historical event");events.push_back(std::move(result.event));}
	const auto before=sharedState_.snapshot();const auto prepared=sharedState_.prepareHistory(channel,events,textCache_);
	if(prepared==before)return 0;
	if(dailyStore_ && !dailyStore_->saveState(state::dailyBook(prepared),state::latestBook(prepared),state::activityBook(prepared)))throw std::runtime_error("History could not be saved; state retained");
	sharedState_.publish(prepared);
	if(stateChanged_)try{stateChanged_();}catch(...){log(LogLevel::Error,"Source refresh failed after history publication");}
	return prepared->channels.at(channel)->activity.size();
}

BatchUpdate PluginCore::processPendingEvents(std::size_t limit)
{
	if (!running()) return {};
	return applyBatch(executionQueue_.prepareBatch(limit));
}
BatchUpdate PluginCore::processPendingEventsFor(events::EventOrigin origin, const std::string &channel, std::size_t limit)
{
	if (!running()) return {};
	return applyBatch(executionQueue_.prepareBatchFor(origin, channel, limit));
}
void PluginCore::selectChannel(state::ChannelKey channel)
{
	if (selectedChannel_.origin == channel.origin && selectedChannel_.channelId == channel.channelId) return;
	selectedChannel_ = std::move(channel);
	if (stateChanged_) stateChanged_();
}
BatchUpdate PluginCore::applyBatch(const execution::PreparedBatch &batch)
{
	if (batch.items().empty()) return {0, sharedState_.snapshot()};
	std::vector<events::EventPtr> events;
	events.reserve(batch.items().size());
	for (const auto &item : batch.items()) events.push_back(item.event);
	const auto prepared = sharedState_.prepare(events, textCache_);
	// Prepare playback ownership before committing: failed persistence must not
	// leak a visual job, and snapshot history limits must not truncate backlog.
	auto presentations=presentationQueue_.prepare(events,textCache_);
	if (dailyStore_) {
		const auto updated = state::dailyBook(prepared);
		const auto latest=state::latestBook(prepared);
		const auto activity=state::activityBook(prepared);
		if ((updated != state::dailyBook(sharedState_.snapshot()) || latest!=state::latestBook(sharedState_.snapshot()) || activity!=state::activityBook(sharedState_.snapshot())) && !dailyStore_->saveState(updated,latest,activity))
			throw std::runtime_error("Daily totals could not be saved; batch retained");
	}
	// All allocating work is complete. No callbacks/reentrancy between commit and publication.
	if (!executionQueue_.commitBatch(batch)) throw std::logic_error("Queue changed during state preparation");
	sharedState_.publish(prepared);
	presentationQueue_.publish(std::move(presentations));
	if (stateChanged_) { try { stateChanged_(); } catch (...) { log(LogLevel::Error, "Source refresh failed after state publication"); } }
	return {batch.items().size(), prepared,std::move(events)};
}
std::string PluginCore::createSourcePreview(bool longText, bool activityList)
{
	if (!running()) throw std::logic_error("Core stopped");
	const auto snapshot = sharedState_.snapshot();
	const auto previews = std::count_if(snapshot->channels.begin(),snapshot->channels.end(),[](const auto &entry) {
		return entry.first.origin == events::EventOrigin::Preview;
	});
	if (previews >= 14 || snapshot->channels.size() >= 15) throw std::length_error("Preview capacity reached");
	if (previewSequence_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Preview IDs exhausted");
	auto channel = "preview-" + std::to_string(++previewSequence_);
	const auto now = std::chrono::system_clock::now();
	// One newest event plus enough older entries to demonstrate overflow at 1–10 rows.
	const std::vector<events::EventType> types = activityList ? std::vector<events::EventType>{events::EventType::Follow,events::EventType::Follow,events::EventType::Follow,events::EventType::Follow,events::EventType::Sub,events::EventType::Raid,events::EventType::Gift,events::EventType::Gift,events::EventType::Sub,events::EventType::Follow,events::EventType::Cheer} : std::vector<events::EventType>{events::EventType::Follow,events::EventType::Sub,events::EventType::Cheer,events::EventType::Gift};
	const char *names[] = {"older_viewer_3","older_viewer_2","older_viewer_1","underground_beats","marinadj","techhouse_tv","itsjavierm","nwoand","ceyhunns","electro_life","dja100"};
	const int ages[] = {1200,1100,1000,900,720,480,300,120,60,25,0};
	std::vector<execution::JobId> jobs; jobs.reserve(types.size());
	try {
		for (std::size_t i=0;i<types.size();++i) {
			const auto type=types[i];
			if (simulationSequence_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Simulation IDs exhausted");
			auto draft = events::makeSimulation(type, ++simulationSequence_, now-std::chrono::seconds(activityList?ages[i]:0));
			draft.origin = events::EventOrigin::Preview; draft.channelId = channel;
			const events::User viewer{"preview-viewer", longText ?
				"TEST_Álex_東京_🎵_Nombre_muy_largo_para_comprobar_recorte_y_Unicode_Sin_Salir_De_La_Columna_1234567890" : activityList?names[i]:"TestViewer"};
			switch (type) {
			case events::EventType::Follow: draft.payload = events::FollowEvent{viewer}; break;
			case events::EventType::Sub: draft.payload = events::SubEvent{viewer}; break;
			case events::EventType::Cheer: draft.payload = events::CheerEvent{viewer,activityList?500ULL:100ULL}; break;
			case events::EventType::Gift: draft.payload = events::GiftEvent{viewer,activityList?(i==6?10ULL:1ULL):5ULL}; break;
			case events::EventType::Raid: draft.payload = events::RaidEvent{viewer,120}; break;
			default: break;
			}
			const auto result = processEvent(std::move(draft));
			if (!result) throw std::runtime_error("Preview queue admission failed");
			jobs.push_back(result.queue.jobId);
		}
		applyBatch(executionQueue_.prepareBatchFor(events::EventOrigin::Preview, channel));
		log(LogLevel::Info, activityList ? "Event List preview delivered through central queue (11 events, visible rows bounded per source)" : longText ? "LONG TEXT preview delivered through central queue (4 events)" : "Source preview delivered through central queue (4 events)");
		return channel;
	} catch (...) { for (auto job : jobs) executionQueue_.cancel(job); throw; }
}
void PluginCore::releaseSourcePreview(const std::string &channel)
{
	const auto before = sharedState_.snapshot();
	if (!before || channel.empty()) return;
	const state::ChannelKey key{events::EventOrigin::Preview,channel};
	if (before->channels.find(key) == before->channels.end()) return;
	auto next = std::make_shared<state::Snapshot>(*before);
	next->channels.erase(key);
	if (next->revision == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("State revision exhausted");
	++next->revision; sharedState_.publish(next);
}
std::string PluginCore::createAlertPreview(events::EventType type,bool pack,bool longText,events::User actor)
{
	if(!running() || state::slot(type)>state::slot(events::EventType::Raid))throw std::invalid_argument("Invalid alert preview");
	const auto snapshot=sharedState_.snapshot();
	const auto previews=std::count_if(snapshot->channels.begin(),snapshot->channels.end(),[](const auto &entry){return entry.first.origin==events::EventOrigin::Preview;});
	if(previews>=14 || snapshot->channels.size()>=15)throw std::length_error("Preview capacity reached");
	if(previewSequence_==std::numeric_limits<std::uint64_t>::max() || simulationSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Preview IDs exhausted");
	const auto channel="preview-"+std::to_string(++previewSequence_);
	auto draft=events::makeSimulation(type,++simulationSequence_,std::chrono::system_clock::now());
	draft.origin=events::EventOrigin::Preview;draft.channelId=channel;
	if(actor.id.empty())actor={"preview-viewer","DJ_PEPITO"};
	if(longText)actor.displayName="TEST_Álex_東京_🎵_Nombre_muy_largo_para_comprobar_recorte_y_Unicode_Sin_Salir_De_La_Columna_1234567890";
	switch(type){
	case events::EventType::Follow:draft.payload=events::FollowEvent{actor};break;
	case events::EventType::Sub:draft.payload=events::SubEvent{actor};break;
	case events::EventType::Cheer:draft.payload=events::CheerEvent{actor,100};break;
	case events::EventType::Gift:draft.payload=events::GiftEvent{actor,pack?10ULL:1ULL};break;
	case events::EventType::Raid:draft.payload=events::RaidEvent{actor,120};break;
	default:break;
	}
	const auto submitted=processEvent(std::move(draft));if(!submitted)throw std::runtime_error("Alert preview admission failed");
	try{applyBatch(executionQueue_.prepareBatchFor(events::EventOrigin::Preview,channel));}
	catch(...){executionQueue_.cancel(submitted.queue.jobId);throw;}
	log(LogLevel::Info,"Animated Alerts TEST delivered through central queue (one event, source isolated)");
	return channel;
}
state::SnapshotPtr PluginCore::sharedSnapshot() const noexcept { return sharedState_.snapshot(); }
std::string PluginCore::createShoutoutPreview(bool longText,events::User actor,std::string login,std::string category,std::optional<std::uint64_t> followers)
{
	if(!running() || sharedState_.snapshot()->channels.size()>=15)throw std::length_error("Shoutout preview capacity reached");
	if(previewSequence_==std::numeric_limits<std::uint64_t>::max() || simulationSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Preview IDs exhausted");
	const auto channel="preview-"+std::to_string(++previewSequence_);
	if(actor.id.empty())actor={"preview-viewer","DemoViewer"};
	if(longText)actor.displayName="TEST_Álex_東京_🎵_Nombre_largo_para_comprobar_el_desplazamiento_1234567890";
	const auto submitted=processEvent({"shoutout-preview-"+std::to_string(++simulationSequence_),channel,std::chrono::system_clock::now(),events::EventOrigin::Preview,events::ShoutoutEvent{std::move(actor),std::move(login),std::move(category),followers}});
	if(!submitted)throw std::runtime_error("Shoutout preview admission failed");
	try{applyBatch(executionQueue_.prepareBatchFor(events::EventOrigin::Preview,channel));}
	catch(...){executionQueue_.cancel(submitted.queue.jobId);throw;}
	return channel;
}
std::string PluginCore::createHypePreview(bool longText)
{
	if(!running() || sharedState_.snapshot()->channels.size()>=15)throw std::length_error("Hype preview capacity reached");
	if(previewSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Preview IDs exhausted");
	const auto channel="preview-"+std::to_string(++previewSequence_);
	try{updateHypePreview(channel,events::HypeTrainStage::Begin,false,longText);return channel;}
	catch(...){releaseSourcePreview(channel);throw;}
}
void PluginCore::updateHypePreview(const std::string &channel,events::HypeTrainStage stage,bool levelUp,bool longText)
{
	if(!running() || channel.rfind("preview-",0)!=0)throw std::invalid_argument("Invalid Hype preview channel");
	if(simulationSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Preview event IDs exhausted");
	const auto now=std::chrono::system_clock::now();
	events::HypeTrainEvent hype{stage,3,8900,5000};hype.trainId=channel;hype.progress=3900;hype.startedAt=now;hype.expiresAt=now+std::chrono::seconds(84);
	hype.contributors={{{"test-a","TEST_TechnoClub"},1500,2,0,1500,3},{{"test-b","TEST_MarinaDJ"},1200,1,1200,0,{}},{{"test-c","TEST_Alex"},750,3,250,500,1}};
	if(longText){hype.contributors[0].user.displayName="TEST_Contributor_With_A_Very_Long_Name";hype.contributors[1].user.displayName="TEST_MarinaDJ_Long_Channel_Name";hype.contributors[2].user.displayName="TEST_Álex_東京_Long_Name";}
	const auto snapshot=sharedState_.snapshot();const auto found=snapshot->channels.find({events::EventOrigin::Preview,channel});
	if(found!=snapshot->channels.end() && found->second->hypeTrain) {
		hype=*found->second->hypeTrain;hype.stage=stage;
		if(stage!=events::HypeTrainStage::End) {
			const auto progress=hype.progress.value_or(0);
			if(progress>std::numeric_limits<std::uint64_t>::max()-250 || hype.total>std::numeric_limits<std::uint64_t>::max()-250)throw std::overflow_error("Hype preview points exhausted");
			if(!levelUp)hype.progress=progress+250;
			// Only the private simulator advances automatically. Live levels remain
			// authoritative EventSub data. Carry surplus points into the new level.
			if(levelUp || (hype.goal>0 && *hype.progress>=hype.goal)){
				if(hype.level==std::numeric_limits<std::uint32_t>::max() || hype.goal>std::numeric_limits<std::uint64_t>::max()-500)throw std::overflow_error("Hype preview levels exhausted");
				hype.progress=levelUp?100:*hype.progress-hype.goal;
				++hype.level;hype.goal+=500;hype.expiresAt=now+std::chrono::seconds(60);
			}
			hype.total+=250;
		}
	}
	const auto submitted=processEvent({"hype-preview-"+std::to_string(++simulationSequence_),channel,now,events::EventOrigin::Preview,std::move(hype)});
	if(!submitted)throw std::runtime_error("Hype preview admission failed");
	try{applyBatch(executionQueue_.prepareBatchFor(events::EventOrigin::Preview,channel));}
	catch(...){executionQueue_.cancel(submitted.queue.jobId);throw;}
}
std::string PluginCore::createChatPreview(bool longText,ChatPreviewEffects effects)
{
	if(!running() || sharedState_.snapshot()->channels.size()>=15)throw std::length_error("Chat preview capacity reached");
	if(previewSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Preview IDs exhausted");
	const auto channel="preview-"+std::to_string(++previewSequence_);
	try{for(int n=0;n<(effects!=ChatPreviewEffects::None?1:6);++n)appendChatPreview(channel,longText && n==5,effects);return channel;}catch(...){releaseSourcePreview(channel);throw;}
}
void PluginCore::appendChatPreview(const std::string &channel,bool longText,ChatPreviewEffects effects)
{
	if(!running() || channel.rfind("preview-",0)!=0)throw std::invalid_argument("Invalid chat preview");
	if(simulationSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Preview event IDs exhausted");
	const auto sequence=++simulationSequence_;const auto n=sequence%5;
	const char *names[]={"TEST_DJ_PEPITO","TEST_MarinaDJ","TEST_TechnoClub","TEST_ModJuan","TEST_Alex"};
	const char *messages[]={"¡Qué tema! 🔥🔥🔥","Brutal, me encanta este directo 💜","Vamos equipo, ¡subid el volumen! 🙌","!so @underground_beats","This message wraps naturally when the available space becomes smaller."};
	const char *colors[]={"#D659FF","#FF59B4","#00D9FF","#39E67A","#A9C8FF"};
	const events::ChatRole roles[]={events::ChatRole::Streamer,events::ChatRole::Subscriber,events::ChatRole::Vip,events::ChatRole::Moderator,events::ChatRole::Viewer};
	events::ChatEvent chat{{"test-chat-"+std::to_string(n),names[n]},messages[n],n==0 || n==3,colors[n],roles[n]};
	chat.messageId="chat-preview-"+std::to_string(sequence);
	const char *badgeSets[]={"broadcaster","subscriber","vip","moderator",""};
	if(n<4)chat.badges.push_back({badgeSets[n],n==1?"0":"1",{}});
	if(n==1)chat.badges.push_back({"bits","1000",{}});
	if(n==0){chat.message="¡Qué tema! Kappa RainTime PETPET";chat.fragments={{"¡Qué tema! ",{},false},{"Kappa","25",false},{" RainTime PETPET",{},false}};}
	if(n==1)chat.message="¡A bailar! SourPls SourPls";
	// Spin belongs to FFZ's per-user sets, not its default global catalog.
	if(effects==ChatPreviewEffects::FFZMirrors){chat.author.displayName="TEST_Effects";chat.message="Kappa ffzX Kappa ffzY Kappa ffzX ffzY";chat.fragments={{"Kappa","25",false},{" ffzX ",{},false},{"Kappa","25",false},{" ffzY ",{},false},{"Kappa","25",false},{" ffzX ffzY",{},false}};}
	if(effects==ChatPreviewEffects::FFZGrow){chat.author.displayName="TEST_FFZ_Wide";chat.message="Kappa Kappa ffzW Kappa ffzW ffzX end";chat.fragments={{"Kappa","25",false},{" ",{},false},{"Kappa","25",false},{" ffzW ",{},false},{"Kappa","25",false},{" ffzW ffzX end",{},false}};}
	if(effects==ChatPreviewEffects::Cheermotes){chat.author.displayName="TEST_BITS";chat.message="Cheer1 Cheer100 Cheer1000 end";chat.fragments={{"Cheer1",{},false,"cheer",1,1},{" ",{},false},{"Cheer100",{},false,"cheer",100,100},{" ",{},false},{"Cheer1000",{},false,"cheer",1000,1000},{" end",{},false}};}
	if(effects==ChatPreviewEffects::FFZCursed){chat.author.displayName="TEST_FFZ_Cursed";chat.message="Kappa Kappa ffzCursed Kappa ffzCursed ffzX Kappa ffzCursed ffzY end";chat.fragments={{"Kappa","25",false},{" ",{},false},{"Kappa","25",false},{" ffzCursed ",{},false},{"Kappa","25",false},{" ffzCursed ffzX ",{},false},{"Kappa","25",false},{" ffzCursed ffzY end",{},false}};}
	if(effects==ChatPreviewEffects::BTTVTransforms){chat.author.displayName="TEST_BTTV";chat.message="h! Kappa v! Kappa l! Kappa r! Kappa";chat.fragments={{"h! ",{},false},{"Kappa","25",false},{" v! ",{},false},{"Kappa","25",false},{" l! ",{},false},{"Kappa","25",false},{" r! ",{},false},{"Kappa","25",false}};}
    if(effects==ChatPreviewEffects::BTTVWide){chat.author.displayName="TEST_WIDE";chat.message="Kappa w! Kappa w! p! s! Kappa w! r! Kappa end";chat.fragments={{"Kappa","25",false},{" w! ",{},false},{"Kappa","25",false},{" w! p! s! ",{},false},{"Kappa","25",false},{" w! r! ",{},false},{"Kappa","25",false},{" end",{},false}};}
	if(effects==ChatPreviewEffects::BTTVMood){chat.author.displayName="TEST_BTTV_FX";chat.message="p! Kappa s! Kappa c! Kappa p! s! Kappa";chat.fragments={{"p! ",{},false},{"Kappa","25",false},{" s! ",{},false},{"Kappa","25",false},{" c! ",{},false},{"Kappa","25",false},{" p! s! ",{},false},{"Kappa","25",false}};}
	if(longText){chat.fragments.clear();chat.author.displayName="TEST_Álex_東京_Nombre_Largo_1234567890";const std::string part="Texto largo con Álex, 東京, música 🎵 y corazones 💜. ";chat.message.clear();for(int i=0;i<6;++i)chat.message+=part;}
	const auto submitted=processEvent({"chat-preview-"+std::to_string(sequence),channel,std::chrono::system_clock::now(),events::EventOrigin::Preview,std::move(chat)});
	if(!submitted)throw std::runtime_error("Chat preview admission failed");
	try{applyBatch(executionQueue_.prepareBatchFor(events::EventOrigin::Preview,channel));}catch(...){executionQueue_.cancel(submitted.queue.jobId);throw;}
}
void PluginCore::moderateChatPreview(const std::string &channel,events::ChatModerationAction action)
{
	const auto snapshot=sharedSnapshot();const auto found=snapshot->channels.find({events::EventOrigin::Preview,channel});
	if(!running() || channel.rfind("preview-",0)!=0 || found==snapshot->channels.end())throw std::invalid_argument("Invalid chat moderation preview");
	if(found->second->chat.empty())return;
	if(simulationSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Preview event IDs exhausted");
	const auto &target=found->second->chat.back().event->data();const auto &chat=std::get<events::ChatEvent>(target.payload);
	events::ChatModerationEvent moderation{action};
	if(action==events::ChatModerationAction::DeleteMessage)moderation.messageId=chat.messageId.empty()?target.id:chat.messageId;
	if(action==events::ChatModerationAction::ClearUser)moderation.userId=chat.author.id;
	const auto submitted=processEvent({"chat-moderation-preview-"+std::to_string(++simulationSequence_),channel,std::chrono::system_clock::now(),events::EventOrigin::Preview,std::move(moderation)});
	if(!submitted)throw std::runtime_error("Chat moderation preview admission failed");
	try{applyBatch(executionQueue_.prepareBatchFor(events::EventOrigin::Preview,channel));}catch(...){executionQueue_.cancel(submitted.queue.jobId);throw;}
}
state::CacheStats PluginCore::cacheStats() const noexcept { return textCache_.stats(); }
bool PluginCore::cancelEvent(execution::JobId id) noexcept { return executionQueue_.cancel(id); }
std::size_t PluginCore::cancelPendingEvents() noexcept { return executionQueue_.cancelAll(); }
std::size_t PluginCore::cancelPendingEventsFor(events::EventOrigin origin,const std::string &channel) noexcept { return executionQueue_.cancelFor(origin,channel); }
std::size_t PluginCore::pendingEventCount() const noexcept { return executionQueue_.size(); }

bool PluginCore::running() const noexcept
{
	return (state_.load() & runningBit) != 0;
}

Settings PluginCore::settings() const noexcept
{
	return Settings{(state_.load() & diagnosticsBit) != 0};
}

void PluginCore::log(LogLevel level, std::string_view message) const noexcept
{
	const auto snapshot = state_.load();
	if (!(snapshot & runningBit) || (level == LogLevel::Debug && !(snapshot & diagnosticsBit)))
		return;
	logger_->write(level, message);
}
} // namespace oto
