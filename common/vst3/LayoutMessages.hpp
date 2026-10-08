#pragma once
#include "../runtime/Layout.hpp"
#include "../state/State.hpp"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include <limits>
#include <cstring>
namespace just {
inline constexpr const char* layoutMessageID="just.layout.v1";
inline constexpr const char* layoutRequestID="just.layout.request.v1";
// Called only on non-audio lifecycle/connection paths. IMessage allocation and
// attribute access never occur in process(). Explicit attributes avoid C++ ABI padding.
inline bool writeLayoutMessage(Steinberg::Vst::IMessage* m,const BusLayoutSnapshot& s,Uid uid) {
    using namespace Steinberg;
    if(!m || !s.valid() || !s.sequence || !s.session || s.session>std::uint64_t(std::numeric_limits<int64>::max()) || s.sequence>std::uint64_t(std::numeric_limits<int64>::max()))return false;
    m->setMessageID(layoutMessageID);auto* a=m->getAttributes();if(!a)return false;
    const char* names[]={"uid0","uid1","uid2","uid3"};
    for(unsigned i=0;i<4;++i)if(a->setInt(names[i],uid.words[i])!=kResultOk)return false;
    return a->setInt("version",s.version)==kResultOk && a->setInt("fields",s.validFields)==kResultOk &&
        a->setInt("session",static_cast<int64>(s.session))==kResultOk && a->setInt("sequence",static_cast<int64>(s.sequence))==kResultOk && a->setFloat("rate",s.sampleRate)==kResultOk &&
        a->setInt("input",s.inputChannels)==kResultOk && a->setInt("output",s.outputChannels)==kResultOk &&
        a->setInt("sc",s.sidechainChannels)==kResultOk && a->setInt("scActive",s.sidechainActive)==kResultOk &&
        a->setInt("active",s.active)==kResultOk && a->setInt("offline",s.offline)==kResultOk;
}
inline bool readLayoutMessage(Steinberg::Vst::IMessage* m,Uid uid,BusLayoutSnapshot& out) {
    using namespace Steinberg;
    if(!m || !m->getMessageID() || std::strcmp(m->getMessageID(),layoutMessageID))return false;
    auto* a=m->getAttributes();if(!a)return false;
    int64 v[14]{};const char* keys[]={"uid0","uid1","uid2","uid3","version","fields","sequence","input","output","sc","scActive","active","offline","session"};
    for(unsigned i=0;i<14;++i)if(a->getInt(keys[i],v[i])!=kResultOk || v[i]<0)return false;
    for(unsigned i=0;i<4;++i)if(std::uint64_t(v[i])!=uid.words[i])return false;
    if(v[4]!=1 || v[5]>3 || !v[6] || v[7]>2 || v[8]>2 || v[9]>2 || v[10]>1 || v[11]>1 || v[12]>1 || !v[13])return false;
    BusLayoutSnapshot s;s.validFields=static_cast<std::uint32_t>(v[5]);s.sequence=static_cast<std::uint64_t>(v[6]);
    s.inputChannels=static_cast<std::uint32_t>(v[7]);s.outputChannels=static_cast<std::uint32_t>(v[8]);s.sidechainChannels=static_cast<std::uint32_t>(v[9]);
    s.sidechainActive=bool(v[10]);s.active=bool(v[11]);s.offline=bool(v[12]);s.session=static_cast<std::uint64_t>(v[13]);
    if(a->getFloat("rate",s.sampleRate)!=kResultOk || !s.valid())return false;
    out=s;return true;
}
inline bool writeLayoutRequest(Steinberg::Vst::IMessage* m,Uid uid,std::uint64_t session) {
    using namespace Steinberg;
    if(!m || !session || session>std::uint64_t(std::numeric_limits<int64>::max()))return false;
    m->setMessageID(layoutRequestID);auto* a=m->getAttributes();if(!a)return false;
    const char* names[]={"uid0","uid1","uid2","uid3"};
    for(unsigned i=0;i<4;++i)if(a->setInt(names[i],uid.words[i])!=kResultOk)return false;
    return a->setInt("session",static_cast<int64>(session))==kResultOk;
}
inline bool readLayoutRequest(Steinberg::Vst::IMessage* m,Uid uid,std::uint64_t& session) {
    using namespace Steinberg;
    if(!m || !m->getMessageID() || std::strcmp(m->getMessageID(),layoutRequestID))return false;
    auto* a=m->getAttributes();if(!a)return false;
    const char* names[]={"uid0","uid1","uid2","uid3"};int64 v;
    for(unsigned i=0;i<4;++i)if(a->getInt(names[i],v)!=kResultOk || v<0 || std::uint64_t(v)!=uid.words[i])return false;
    if(a->getInt("session",v)!=kResultOk || v<=0)return false;
    session=static_cast<std::uint64_t>(v);return true;
}
}
