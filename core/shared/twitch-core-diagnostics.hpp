#pragma once
#include "twitch-core-runtime.h"
#include "twitch-core-version.h"
#include <QString>
#include <cstring>
#include <QVersionNumber>
namespace nexus {
inline bool compatibleCore(const NexusTwitchCoreInfo &info,const char *minimum=NEXUS_TITLE_MIN_VERSION,const char *maximum=NEXUS_TITLE_MAX_VERSION,uint32_t abi=NEXUS_TITLE_ABI){
 int suffix=0;const auto version=QVersionNumber::fromString(QString::fromLatin1(info.version),&suffix);
 return info.abi==abi&&suffix==int(std::strlen(info.version))&&version.segmentCount()==3&&
  QVersionNumber::compare(version,QVersionNumber::fromString(QString::fromLatin1(minimum)))>=0&&QVersionNumber::compare(version,QVersionNumber::fromString(QString::fromLatin1(maximum)))<0;
}
inline QString coreLabel(const NexusTwitchCoreInfo &info){return QStringLiteral("Twitch Core %1 · ABI %2").arg(QString::fromLatin1(info.version)).arg(info.abi);}
inline QString corePath(const NexusTwitchCoreInfo &info){return QString::fromUtf16(info.module_path);}
}
