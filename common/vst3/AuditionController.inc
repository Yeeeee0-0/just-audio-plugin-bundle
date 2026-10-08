bool Controller::sendAudition(std::uint64_t token,ParamID target,unsigned action){
    if(!getPeer() || !layoutSession)return false;auto m=owned(allocateMessage());if(!m)return false;auto* a=m->getAttributes();if(!a)return false;
    m->setMessageID(auditionRequestID);a->setInt("Session",layoutSession);a->setInt("Token",token);a->setInt("Target",target);a->setInt("Action",action);a->setInt("Deadline",analysisNow()+250000000ull);return sendMessage(m)==kResultOk;
}
std::uint64_t Controller::beginAudition(ParamID target){
    if(!editorCount || !moduleDefinition().validateAuditionTarget || !moduleDefinition().validateAuditionTarget(target))return 0;
    static std::atomic<std::uint64_t> next{0};auto token=++next;cancelAudition();
    if(!sendAudition(token,target,0))return 0;audition={token,target,AuditionPhase::pending};return token;
}
bool Controller::renewAudition(std::uint64_t token){return token && token==audition.token && (audition.phase==AuditionPhase::pending || audition.phase==AuditionPhase::active) && sendAudition(token,audition.bandAnchor,1);}
void Controller::endAudition(std::uint64_t token){if(!token || token!=audition.token)return;sendAudition(token,audition.bandAnchor,2);audition.phase=AuditionPhase::ended;}
