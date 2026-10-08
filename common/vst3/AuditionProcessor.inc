tresult Processor::handleAuditionMessage(IMessage* m){
    if(!messageSession(m,layoutSession))return kInvalidArgument;auto* a=m->getAttributes();int64 token=0,target=0,action=0,deadline=0;
    if(a->getInt("Token",token)!=kResultOk || token<=0 || a->getInt("Target",target)!=kResultOk || target<0 || target>0xffffffffll ||
        a->getInt("Action",action)!=kResultOk || action<0 || action>2 || a->getInt("Deadline",deadline)!=kResultOk || deadline<=0)return kInvalidArgument;
    const auto now=analysisNow();if(action!=2 && (std::uint64_t(deadline)<=now || std::uint64_t(deadline)-now>250000000ull))return kResultFalse;
    if(action==0){if(!moduleDefinition().validateAuditionTarget || !moduleDefinition().validateAuditionTarget(ParamID(target)))return kResultFalse;
        if(auditionMain.token && std::uint64_t(token)<=auditionMain.token)return kResultFalse;
        auditionMain.token=token;auditionMain.bandAnchor=ParamID(target);auditionMain.session=layoutSession;auditionMain.enabled=1;
    }else {if(auditionMain.token!=std::uint64_t(token) || auditionMain.session!=layoutSession)return kResultFalse;
        if(action==1 && (!auditionMain.enabled || analysisNow()>auditionDeadline))return kResultFalse;
        if(action==2)auditionMain.enabled=0;
    }
    ++auditionMain.renewal;auditionDeadline=auditionMain.deadlineNanoseconds=deadline;auditionPending.publish(auditionMain);return kResultOk;
}
void Processor::endAudioAudition() noexcept{
    if(auditionState.phase==AuditionPhase::active)engine->setAudition(auditionState.bandAnchor,false);
    if(auditionState.token)auditionState.phase=AuditionPhase::ended;auditionRemaining=0;auditionPublication.publish(auditionState);
}
void Processor::updateAudition() noexcept{
    AuditionRequest request;if(!auditionPending.read(request))return;
    if(!request.enabled || request.session!=telemetrySession.load(std::memory_order_acquire)){endAudioAudition();return;}
    const auto now=analysisNow();
    if(!request.deadlineNanoseconds || now>=request.deadlineNanoseconds){
        endAudioAudition();auditionAudio=request;auditionState={request.token,request.bandAnchor,AuditionPhase::ended};auditionPublication.publish(auditionState);return;
    }
    if(request.token!=auditionAudio.token){endAudioAudition();auditionState={request.token,request.bandAnchor,AuditionPhase::pending};
        auditionState.phase=engine->setAudition(request.bandAnchor,true)?AuditionPhase::active:AuditionPhase::rejected;
    }
    if(request.renewal!=auditionAudio.renewal && auditionState.phase==AuditionPhase::active)auditionRemaining=std::uint64_t(prepared.sampleRate*.25);
    if(auditionState.phase==AuditionPhase::active)auditionRemaining=std::min(auditionRemaining,std::uint64_t(double(request.deadlineNanoseconds-now)*prepared.sampleRate/1e9));
    auditionAudio=request;auditionPublication.publish(auditionState);
}
