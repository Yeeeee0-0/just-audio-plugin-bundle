#pragma once
#include "Filter.hpp"
#include "AnalyzerModel.hpp"
#include "common/ui/DisplayFormat.hpp"
#include "common/ui/Controls.hpp"
#include "common/vst3/Module.hpp"
#include <vector>
#include <string>
namespace just::eq {
struct FieldStyle {std::uint32_t rgb;int dash;const char* label;const char* node;};
inline constexpr FieldStyle fieldStyles[]={{0x178F98,0,"Stereo","circle"},{0x429557,1,"Mid","circle+M"},{0x239ABF,2,"Side","circle+S"}};
// This model has no engine access. View switching/selection/painting never writes sound.
class EditorModel {
    EditorServices services{};ParamID dragFrequency=0,dragGain=0;bool editingFrequency=false,editingGain=false;
    std::uint64_t visualResumeGeneration=0;
public:
    AnalyzerModel analyzer;
    void configureAnalyzer(const AnalyzerSettings& settings) {
        if(services.writeAnalyzerPreferences && services.writeAnalyzerPreferences(services.owner,settings))analyzer.configure(settings);
    }
    const EditorServices& editorServices() const {return services;}
    bool isCut() const {return int(value(type))==3 || int(value(type))==4;}
    // One discrete wheel step selects a real, legacy slope. Never expand the
    // five-value normalized mapping to match unsupported preview choices.
    void wheel(double steps,bool fine=false) {
        if(!panelVisible || steps==0)return;
        if(isCut())writeSlopeChoice(std::clamp(slopeChoice()+(steps>0?1:-1),0,7));
        else write(q,std::clamp(value(q)*std::exp(steps*(fine?.002:.02)),.1,20.));
    }
    int slopeChoice() const {constexpr int choices[]={0,1,3,4,5,2,6,7};return choices[effectiveSlope(state,selected)];}
    void writeSlopeChoice(int choice) {
        constexpr int legacy[]={0,1,-1,2,3,4,-1,-1};
        choice=std::clamp(choice,0,7);
        if(legacy[choice]>=0){
            const ParamID ids[]={id(selected,slope),id(selected,slopeExtension)};
            const double values[]={parameters[index(selected,slope)].toNormalized(legacy[choice]),0};
            just::MultiParameterGesture gesture(services);
            if(gesture.begin(ids,2))gesture.write(values,2);gesture.end();refresh();
        }
        else write(slopeExtension,choice==2?1:choice==6?2:3);
    }
    void beginSoloSweep() {
        cancelDrag();dragFrequency=id(selected,frequency);
        editingFrequency=services.beginEdit && services.beginEdit(services.owner,dragFrequency);
    }
    void sweepSolo(double hz) {
        if(soloToken && editingFrequency && services.performEdit)
            services.performEdit(services.owner,dragFrequency,parameters[index(selected,frequency)].toNormalized(hz));
        refresh();
    }
    BusLayoutSnapshot layout{};
    AnalysisCursor analysisCursor{};AnalysisWindow latestWindow{};bool hasWindow=false;
    AnalysisAvailability historyAvailability=AnalysisAvailability::unavailable;
    AuditionStatus soloStatus{};std::uint64_t soloToken=0;
    double responseRate() const {return (layout.validFields&layoutSampleRate)?layout.sampleRate:48000;}
    bool beginSolo() {endSolo();if(value(enabled)<0.5 || !services.beginAudition)return false;soloToken=services.beginAudition(services.owner,id(selected,frequency));soloStatus={soloToken,id(selected,frequency),soloToken?AuditionPhase::pending:AuditionPhase::unavailable};return soloToken!=0;}
    bool renewSolo() {return soloToken && services.renewAudition && services.renewAudition(services.owner,soloToken);}
    void endSolo() {if(soloToken && services.endAudition)services.endAudition(services.owner,soloToken);if(soloToken)cancelDrag();soloToken=0;soloStatus.phase=AuditionPhase::ended;}
    bool canSolo() const {return services.beginAudition && (layout.validFields&layoutBuses) && layout.active && value(enabled)>0.5;}
    SoundState state=initialState({},registry);std::size_t selected=0;bool advanced=false,panelVisible=false;
    void connect(EditorServices s) {services=s;refresh();panelVisible=value(enabled)>0.5;}
    void select(std::size_t b) {endSolo();cancelDrag();selected=std::min(b,bandCount-1);panelVisible=true;}
    void hidePanel() {endSolo();cancelDrag();panelVisible=false;}
    void refresh() {
        const double previousShape=value(type),previousTarget=value(target),previousSlope=effectiveSlope(state,selected);
        if(services.readTarget)for(std::size_t i=0;i<registry.count;++i)state.targets[i]=services.readTarget(services.owner,parameters[i].id);
        if(soloToken && (value(type)!=previousShape || value(target)!=previousTarget || effectiveSlope(state,selected)!=previousSlope))endSolo();
        advanced=services.view && services.view->advanced;
        AnalyzerPreferences preferences;if(services.readAnalyzerPreferences && services.readAnalyzerPreferences(services.owner,preferences))analyzer.configure(preferences);
        layout={};if(services.readBusLayout)services.readBusLayout(services.owner,layout);
        // Only measurement presentation pauses. Targets, layout and audition
        // status above/below remain live, including while an edit is held.
        if(!services.view || !services.view->visualsPaused){
            const auto generation=services.view?services.view->visualResumeGeneration:0;
            if(generation!=visualResumeGeneration){
                analysisCursor={};latestWindow={};hasWindow=false;historyAvailability=AnalysisAvailability::unavailable;
                visualResumeGeneration=generation;
                analyzer.reset();
            }
            ExtendedSpectrumSnapshot measured;const auto measuredStatus=services.readExtendedSpectrum?services.readExtendedSpectrum(services.owner,measured):AnalysisAvailability::unavailable;
            for(unsigned attempt=0;attempt<128 && services.readAnalysis;++attempt){AnalysisBatch batch;historyAvailability=services.readAnalysis(services.owner,analysisCursor,batch);if(historyAvailability!=AnalysisAvailability::fresh || !batch.count)break;latestWindow=batch.windows[batch.count-1];hasWindow=true;}
            analyzer.update(measured,measuredStatus,double(analysisNow())*1e-9);
        }
        if(services.readAuditionStatus)soloStatus=services.readAuditionStatus(services.owner);
        if(soloToken && (value(enabled)<0.5 || (soloStatus.token==soloToken && (soloStatus.phase==AuditionPhase::ended || soloStatus.phase==AuditionPhase::rejected))))endSolo();
    }
    double value(Field f) const {return physical(state,index(selected,f));}
    bool beginValueGesture(ParamID p) {return services.beginEdit && services.beginEdit(services.owner,p);}
    void updateValueGesture(ParamID p,double n) {if(services.performEdit)services.performEdit(services.owner,p,n);refresh();}
    void endValueGesture(ParamID p) {if(services.endEdit)services.endEdit(services.owner,p);}
    void writeID(ParamID p,double normalized) {
        if(services.beginEdit && services.beginEdit(services.owner,p)) {
            services.performEdit(services.owner,p,normalized);services.endEdit(services.owner,p);
        }
        refresh();
    }
    void write(std::size_t b,Field f,double v) {auto i=index(b,f);writeID(parameters[i].id,parameters[i].toNormalized(v));}
    void write(Field f,double v) {write(selected,f,v);}
    bool parse(Field f,std::string text) {
        auto i=index(selected,f);double normalized=0;
        // Accept k/kHz frequency entry as well as the shared exact-unit parser.
        if(f==frequency) {
            char* end=nullptr;double v=std::strtod(text.c_str(),&end);
            while(*end==' ')++end;
            if(std::strcmp(end,"k")==0 || std::strcmp(end,"kHz")==0){v*=1000;char buf[64];std::snprintf(buf,sizeof(buf),"%.12g",v);text=buf;}
        }
        if(!parameters[i].parse(text.c_str(),normalized))return false;
        writeID(parameters[i].id,normalized);return true;
    }
    std::array<double,2> finalResponse(double hz,double sampleRate=48000) const {
        auto d=response(hz,sampleRate);return {d[0]+d[1],d[0]+d[2]};
    }
    static std::string display(std::size_t i,double normalized,bool exact=false) {
        const auto& p=parameters[i];char text[96];DisplayPolicy policy;
        if(i>=3 && (i-3)%fieldsPerBand==q)policy.decimals=3;
        formatDisplay(p,normalized,exact?DisplayContext::editing:DisplayContext::simple,policy,text,sizeof(text));return text;
    }
    bool duplicate() {
        refresh();const auto saved=state;const auto from=selected;
        for(std::size_t b=0;b<bandCount;++b)if(physical(state,index(b,enabled))<0.5 && b!=from){
            for(std::size_t f=1;f<fieldsPerBand;++f)write(b,Field(f),physical(saved,index(from,Field(f))));
            write(b,slopeExtension,physical(saved,index(from,slopeExtension)));
            write(b,enabled,1);select(b);return true;
        }return false;
    }
    void create(double hz) {
        refresh();for(std::size_t b=0;b<bandCount;++b)if(physical(state,index(b,enabled))<0.5) {
            selected=b;panelVisible=true;
            for(std::size_t f=0;f<fieldsPerBand;++f)write(b,Field(f),parameters[index(b,Field(f))].initial);
            write(slopeExtension,0);write(frequency,hz);write(enabled,1);return;
        }
    }
    void beginDrag(std::size_t b) {
        select(b);dragFrequency=id(b,frequency);dragGain=id(b,gain);
        editingFrequency=services.beginEdit && services.beginEdit(services.owner,dragFrequency);
        Shape shape=Shape(int(value(type)));
        if(int(shape)<=2)editingGain=services.beginEdit && services.beginEdit(services.owner,dragGain);
    }
    void drag(double hz,double db) {
        if(editingFrequency)services.performEdit(services.owner,dragFrequency,parameters[index(selected,frequency)].toNormalized(hz));
        if(editingGain)services.performEdit(services.owner,dragGain,parameters[index(selected,gain)].toNormalized(db));
        refresh();
    }
    void cancelDrag() {
        if(editingFrequency)services.endEdit(services.owner,dragFrequency);
        if(editingGain)services.endEdit(services.owner,dragGain);
        editingFrequency=editingGain=false;
    }
    ~EditorModel(){endSolo();cancelDrag();}
    std::array<double,3> response(double hz,double referenceFs=48000) const {
        std::array<double,3> db{};
        for(std::size_t b=0;b<bandCount;++b)if(physical(state,index(b,enabled))>0.5) {
            FilterBank filter;filter.update(Shape(int(physical(state,index(b,type)))),physical(state,index(b,frequency)),physical(state,index(b,gain)),physical(state,index(b,q)),effectiveSlope(state,b),referenceFs);
            db[int(physical(state,index(b,target)))]+=filter.db(hz,referenceFs);
        }
        return db;
    }
    bool active(int field) const {
        for(std::size_t b=0;b<bandCount;++b)if(physical(state,index(b,enabled))>0.5 && int(physical(state,index(b,target)))==field)return true;
        return false;
    }
};
}
