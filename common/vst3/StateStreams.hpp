#pragma once
#include "../state/State.hpp"
#include "pluginterfaces/base/ibstream.h"
namespace just {
inline bool readExact(Steinberg::IBStream* stream,void* data,Steinberg::int32 bytes) {
    auto* p=static_cast<std::uint8_t*>(data);
    while(bytes>0){Steinberg::int32 count=0;stream->read(p,bytes,&count);if(count<=0 || count>bytes)return false;p+=count;bytes-=count;}
    return true;
}
inline bool readSoundState(Steinberg::IBStream* stream,Uid id,ParameterRegistry r,SoundState& state) {
    if(!stream)return false;
    std::array<std::uint8_t,maxStateBytes> bytes{};
    if(!readExact(stream,bytes.data(),44))return false;
    auto u32=[&](int offset){std::uint32_t n=0;for(int i=0;i<4;++i)n|=std::uint32_t(bytes[offset+i])<<(i*8);return n;};
    const auto count=u32(36),configs=u32(40);
    if(count>maxParameters || configs>64)return false;
    auto length=44+12*(count+configs);
    if(!readExact(stream,bytes.data()+44,static_cast<Steinberg::int32>(length-44)))return false;
    return decodeState(bytes.data(),length,id,r,state)==StateResult::ok;
}
inline bool writeSoundState(Steinberg::IBStream* stream,const SoundState& state,ParameterRegistry r) {
    if(!stream)return false;auto bytes=encodeState(state,r);
    std::size_t at=0;
    while(at<bytes.size()) {
        Steinberg::int32 count=0;stream->write(bytes.data()+at,static_cast<Steinberg::int32>(bytes.size()-at),&count);
        if(count<=0 || std::size_t(count)>bytes.size()-at)return false;at+=count;
    }
    return true;
}
}
