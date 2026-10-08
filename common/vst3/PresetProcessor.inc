// Included once by Processor.cpp, inside namespace just.
void Processor::sendPresetPublication(const PresetPublication& p) {
    if(!getPeer() || !layoutSession)return;
    auto m=owned(allocateMessage());if(!m)return;m->setMessageID(presetStateID);auto* a=m->getAttributes();if(!a)return;
    a->setInt("Session",layoutSession);a->setInt("Epoch",p.epoch);a->setInt("Transaction",p.transaction);a->setInt("Status",static_cast<int64>(p.status));
    const auto bytes=encodeState(p.state,registry);a->setBinary("State",bytes.data(),static_cast<uint32>(bytes.size()));sendMessage(m);
}
void Processor::onTimer(Steinberg::Timer*) {
    if(auditionMain.enabled && analysisNow()>auditionDeadline){auditionMain.enabled=0;auditionPending.publish(auditionMain);}
    if(getPeer() && layoutSession){AuditionStatus s;if(auditionPublication.read(s) && s.token){auto m=owned(allocateMessage());if(m){m->setMessageID(auditionStatusID);auto* a=m->getAttributes();a->setInt("Session",layoutSession);a->setBinary("Status",&s,sizeof(s));sendMessage(m);}}}
    PresetPublication p;if(presetPublication.read(p))sendPresetPublication(p);
}
tresult Processor::handlePresetMessage(IMessage* message) {
    if(!messageSession(message,layoutSession))return kInvalidArgument;
    PresetPublication active;if(!presetPublication.read(active))return kResultFalse;
    if(!std::strcmp(message->getMessageID(),presetQueryID)){sendPresetPublication(active);return kResultOk;}
    auto* a=message->getAttributes();int64 tx=0,expected=0;const void* bytes=nullptr;uint32 length=0;
    if(a->getInt("Transaction",tx)!=kResultOk || tx<=0 || std::uint64_t(tx)<=lastPresetRequest ||
       a->getInt("Epoch",expected)!=kResultOk || expected<=0 || a->getBinary("State",bytes,length)!=kResultOk)return kInvalidArgument;
    lastPresetRequest=tx;
    auto reject=[&](){active.transaction=tx;active.status=PresetTransactionStatus::rejected;sendPresetPublication(active);return kResultFalse;};
    SoundState desired;if(decodeState(static_cast<const std::uint8_t*>(bytes),length,identity().processor,registry,desired)!=StateResult::ok ||
        length<44 || length!=44+12*(registry.count+desired.configurationCount) || !validCompleteState(desired,identity().processor,registry))return reject();
    // Exact canonical completeness also rejects missing/unknown parameter entries.
    auto canonical=encodeState(desired,registry);if(canonical.size()!=length || std::memcmp(canonical.data(),bytes,length))return reject();
    if(active.epoch!=std::uint64_t(expected) || latestRestore.load()!=active.state.revision ||
       (requestedPreset.load() && requestedPreset.load()!=active.transaction))return reject();
    const auto validator=moduleDefinition().validatePresetState;
    if(validator){if(!validator(desired))return reject();}
    else {
        if(desired.configurationCount!=active.state.configurationCount)return reject();
        for(unsigned i=0;i<desired.configurationCount;++i)if(desired.configurations[i].id!=active.state.configurations[i].id || desired.configurations[i].value!=active.state.configurations[i].value)return reject();
        for(unsigned i=0;i<registry.count;++i)if(registry.specs[i].transition==Transition::preparedConfiguration && desired.targets[i]!=active.state.targets[i])return reject();
    }
    PresetRequest request;request.session=layoutSession;request.transaction=tx;request.expectedEpoch=expected;request.expectedRestore=latestRestore.load();request.desired=desired;
    presetPending.publish(request);requestedPreset.store(tx,std::memory_order_release);return kResultOk;
}
