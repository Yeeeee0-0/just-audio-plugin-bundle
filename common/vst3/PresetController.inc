// Included once by Controller.cpp, inside namespace just.
void Controller::queryCompleteState(){
    if(!getPeer() || !layoutSession)return;auto m=owned(allocateMessage());if(!m)return;m->setMessageID(presetQueryID);m->getAttributes()->setInt("Session",layoutSession);sendMessage(m);
}
bool Controller::readCompleteSoundState(SoundState& out){
    queryCompleteState();if(!hasComplete || !layoutConnected)return false;out=completeState;return true;
}
bool Controller::capturePresetState(SoundState& out){
    SoundState current;if(pendingPreset || !readCompleteSoundState(current))return false;
    // Never mix a new controller target vector with old audio configurations or
    // save stale knob values. Processor publishes the complete state each block.
    for(unsigned i=0;i<registry.count;++i)if(current.targets[i]!=getParamNormalized(registry.specs[i].id))return false;
    out=current;return true;
}
bool Controller::requestApplySoundState(const SoundState& desired){
    if(pendingPreset || !validCompleteState(desired,pluginIdentities[JUST_PLUGIN_INDEX].processor,registry))return false;
    SoundState before;if(!readCompleteSoundState(before))return false;
    if(moduleDefinition().validatePresetState && !moduleDefinition().validatePresetState(desired))return false;
    auto m=owned(allocateMessage());if(!m)return false;auto* a=m->getAttributes();if(!a)return false;
    static std::atomic<std::uint64_t> globalTransaction{0};
    presetBefore=before;presetDesired=desired;pendingPreset=++globalTransaction;presetStatus=PresetTransactionStatus::pending;undoValid=false;
    presetTargetRevisions=targetRevisions;
    m->setMessageID(presetRequestID);a->setInt("Session",layoutSession);a->setInt("Transaction",pendingPreset);a->setInt("Epoch",completeEpoch);
    auto bytes=encodeState(desired,registry);a->setBinary("State",bytes.data(),static_cast<uint32>(bytes.size()));
    const auto result=sendMessage(m);if(result!=kResultOk){pendingPreset=0;presetStatus=PresetTransactionStatus::rejected;pendingUndo=false;return false;}return true;
}
bool Controller::canUndoLastPreset() noexcept{
    if(!undoValid || pendingPreset || !hasComplete || !layoutConnected || !sameSoundState(completeState,undoAfter,registry))return false;
    for(unsigned i=0;i<registry.count;++i)if(getParamNormalized(registry.specs[i].id)!=undoAfter.targets[i])return false;
    return true;
}
bool Controller::undoLastPreset(){if(!canUndoLastPreset())return false;const auto desired=undoState;pendingUndo=true;const bool ok=requestApplySoundState(desired);if(!ok)pendingUndo=false;return ok;}
tresult Controller::receivePresetState(IMessage* message){
    if(!layoutConnected || !messageSession(message,layoutSession))return kResultFalse;
    auto* a=message->getAttributes();int64 epoch=0,tx=0,status=0;const void* bytes=nullptr;uint32 length=0;
    if(a->getInt("Epoch",epoch)!=kResultOk || epoch<=0 || a->getInt("Transaction",tx)!=kResultOk || tx<0 ||
       a->getInt("Status",status)!=kResultOk || status<0 || status>4 || a->getBinary("State",bytes,length)!=kResultOk)return kInvalidArgument;
    SoundState s;if(decodeState(static_cast<const std::uint8_t*>(bytes),length,pluginIdentities[JUST_PLUGIN_INDEX].processor,registry,s)!=StateResult::ok)return kInvalidArgument;
    if(hasComplete && std::uint64_t(epoch)<completeEpoch)return kResultFalse;
    if(undoValid && !sameSoundState(s,undoAfter,registry))undoValid=false;
    completeState=s;completeEpoch=epoch;hasComplete=true;
    if(pendingPreset && std::uint64_t(tx)==pendingPreset && (status==int64(PresetTransactionStatus::applied) || status==int64(PresetTransactionStatus::rejected))){
        presetStatus=static_cast<PresetTransactionStatus>(status);pendingPreset=0;
        if(presetStatus==PresetTransactionStatus::applied){
            undoState=presetBefore;undoAfter=s;undoValid=!pendingUndo && sameSoundState(s,presetDesired,registry);
            for(unsigned i=0;i<registry.count;++i)if(targetRevisions[i]!=presetTargetRevisions[i])undoValid=false;
            FUnknownPtr<IComponentHandler2> group(componentHandler);const bool grouped=group && group->startGroupEdit()==kResultOk;
            for(unsigned i=0;i<registry.count;++i){auto id=registry.specs[i].id;if(targetRevisions[i]!=presetTargetRevisions[i] || getParamNormalized(id)==s.targets[i])continue;
                const bool began=beginEdit(id)==kResultOk;
                if(targetRevisions[i]!=presetTargetRevisions[i]){if(began)endEdit(id);continue;}
                EditController::setParamNormalized(id,s.targets[i]);
                if(began){applyingPresetNotification=true;performEdit(id,s.targets[i]);applyingPresetNotification=false;endEdit(id);}
            }
            if(group){if(grouped)group->finishGroupEdit();group->setDirty(true);}
        }
        pendingUndo=false;
    } else if(!pendingPreset && presetStatus==PresetTransactionStatus::unavailable)presetStatus=PresetTransactionStatus::idle;
    return kResultOk;
}
