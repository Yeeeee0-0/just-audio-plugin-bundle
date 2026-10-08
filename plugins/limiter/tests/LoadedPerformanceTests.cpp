// Non-GUI actual-module timing. No production DSP headers or reconstruction
// measurement is linked into this host. The timed region is only process().
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <mach/mach.h>
#include <array>
#include <vector>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numeric>
using namespace Steinberg;using namespace Steinberg::Vst;
using Clock=std::chrono::steady_clock;using Bytes=std::vector<std::uint8_t>;
static void require(bool value,const char* why){if(!value){std::cerr<<why<<'\n';std::exit(1);}}
static std::uint32_t integer(const Bytes& b,unsigned at){std::uint32_t n=0;require(at+4<=b.size(),"state bounds");for(unsigned k=0;k<4;++k)n|=std::uint32_t(b[at+k])<<(k*8);return n;}
static void set(Bytes& b,unsigned id,double v){for(unsigned i=0;i<integer(b,36);++i)if(integer(b,44+12*i)==id){std::memcpy(b.data()+48+12*i,&v,8);return;}require(false,"existing state ID");}
static double threadSeconds(){thread_basic_info_data_t info{};mach_msg_type_number_t count=THREAD_BASIC_INFO_COUNT;auto thread=mach_thread_self();const auto result=thread_info(thread,THREAD_BASIC_INFO,reinterpret_cast<thread_info_t>(&info),&count);mach_port_deallocate(mach_task_self(),thread);require(result==KERN_SUCCESS,"thread CPU time");return info.user_time.seconds+info.system_time.seconds+(info.user_time.microseconds+info.system_time.microseconds)*1e-6;}
static std::ofstream callbackCsv;
struct Stats{double mean=0,p99=0,worst=0;unsigned misses=0;};
static Stats stats(std::vector<double> ns,double deadline){Stats s;s.mean=std::accumulate(ns.begin(),ns.end(),0.)/ns.size();std::sort(ns.begin(),ns.end());s.p99=ns[std::min(ns.size()-1,std::size_t(std::ceil(ns.size()*.99))-1)];s.worst=ns.back();s.misses=std::count_if(ns.begin(),ns.end(),[&](double x){return x>deadline;});return s;}
template<class T>static void run(VST3::Hosting::Module& module,HostApplication& app,const char* label,int block,unsigned mode,std::ofstream& csv,unsigned measured,bool detailed=false){
 auto infos=module.getFactory().classInfos();auto c=module.getFactory().createInstance<IComponent>(infos[0].ID());require(c&&c->initialize(&app)==kResultOk,"component");FUnknownPtr<IAudioProcessor> p(c);require(bool(p),"processor");MemoryStream defaults;require(c->getState(&defaults)==kResultOk,"defaults");auto raw=reinterpret_cast<const std::uint8_t*>(defaults.getData());Bytes state(raw,raw+defaults.getSize());set(state,7,mode);MemoryStream restore(state.data(),state.size());require(c->setState(&restore)==kResultOk,"mode state");
 constexpr double fs=48000;const auto format=std::is_same_v<T,float>?kSample32:kSample64;SpeakerArrangement stereo=SpeakerArr::kStereo;require(p->setBusArrangements(&stereo,1,&stereo,1)==kResultOk,"bus");ProcessSetup setup{kRealtime,format,block,fs};require(p->setupProcessing(setup)==kResultOk&&c->setActive(true)==kResultOk,"prepare");auto started=p->setProcessing(true);require(started==kResultOk||started==kNotImplemented,"start");
 constexpr unsigned period=65536;std::array<std::vector<T>,2> source,output;for(auto&ch:source)ch.resize(period);for(auto&ch:output)ch.resize(block);for(unsigned i=0;i<period;++i){const double drive=i%8192<2048?2.3:.25;source[0][i]=T(drive*(.63*std::sin(i*.13)+.31*std::cos(i*2.719))+(i%4001==0?3.:0));source[1][i]=T(-.43*source[0][i]+.11*std::sin(i*.91));}
 T* ins[]={source[0].data(),source[1].data()};T* outs[]={output[0].data(),output[1].data()};AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;if constexpr(std::is_same_v<T,float>){in.channelBuffers32=ins;out.channelBuffers32=outs;}else{in.channelBuffers64=ins;out.channelBuffers64=outs;}
 ProcessContext context{};context.state=ProcessContext::kPlaying;context.sampleRate=fs;ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=format;data.numSamples=block;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;
 std::vector<double> timings(measured),cpuTimings(measured);constexpr unsigned warm=256;double checksum=0;double cpuStart=0,cpuSeconds=0;const auto begin=Clock::now();
 for(unsigned i=0;i<measured+warm;++i){const unsigned position=(i*block)%period;ins[0]=source[0].data()+position;ins[1]=source[1].data()+position;context.projectTimeSamples=int64(i)*block;if(i==warm)cpuStart=threadSeconds();
  const double cpu0=detailed?threadSeconds():0;const auto t0=Clock::now();const auto ok=p->process(data);const auto t1=Clock::now();const double cpu1=detailed?threadSeconds():0;
  require(ok==kResultOk,"process");if(i>=warm){timings[i-warm]=std::chrono::duration<double,std::nano>(t1-t0).count();cpuTimings[i-warm]=(cpu1-cpu0)*1e9;}checksum+=output[0][i%block];
 }
 cpuSeconds=threadSeconds()-cpuStart;require(std::isfinite(checksum),"finite checksum");const double totalWall=std::chrono::duration<double>(Clock::now()-begin).count();const double deadline=block/fs*1e9;const auto s=stats(timings,deadline);csv<<label<<','<<sizeof(T)*8<<','<<mode<<','<<block<<','<<measured<<','<<p->getLatencySamples()<<','<<deadline/1000<<','<<s.mean/1000<<','<<s.p99/1000<<','<<s.worst/1000<<','<<100*s.mean/deadline<<','<<100*s.p99/deadline<<','<<100*s.worst/deadline<<','<<s.misses<<','<<cpuSeconds<<','<<totalWall<<','<<checksum<<'\n';csv.flush();std::cout<<label<<" bits="<<sizeof(T)*8<<" mode="<<mode<<" block="<<block<<" mean/p99/worst_us="<<s.mean/1000<<'/'<<s.p99/1000<<'/'<<s.worst/1000<<" deadline_us="<<deadline/1000<<" mean%="<<100*s.mean/deadline<<" misses="<<s.misses<<'\n';std::cout.flush();if(detailed){for(unsigned i=0;i<measured;++i)callbackCsv<<label<<','<<block<<','<<i<<','<<timings[i]/1000<<','<<cpuTimings[i]/1000<<','<<deadline/1000<<'\n';callbackCsv.flush();const auto cs=stats(cpuTimings,deadline);auto worstAt=std::max_element(timings.begin(),timings.end())-timings.begin();std::cout<<"CPU_DETAIL block="<<block<<" mean/p99/worst_us="<<cs.mean/1000<<'/'<<cs.p99/1000<<'/'<<cs.worst/1000<<" cpu_misses="<<cs.misses<<" at_worst_wall_cpu_us="<<cpuTimings[worstAt]/1000<<" at_worst_wall_us="<<timings[worstAt]/1000<<" (raw, includes two thread-info query bookkeeping costs)\n";}p->setProcessing(false);c->setActive(false);c->terminate();
}
int main(int argc,char**argv){require(argc==4||argc==5,"usage label absolute-bundle output.csv [detailed]");const bool detailed=argc==5;std::cout<<std::setprecision(9);std::vector<double> empty(20000);for(auto&v:empty){auto a=Clock::now();auto b=Clock::now();v=std::chrono::duration<double,std::nano>(b-a).count();}auto overhead=stats(empty,1e9);std::cout<<"clock_pair_ns mean="<<overhead.mean<<" p99="<<overhead.p99<<" worst="<<overhead.worst<<" (not subtracted); no verifier/signal generation/IO inside timed region\n";
 std::string error;auto module=VST3::Hosting::Module::create(argv[2],error);require(bool(module),error.c_str());HostApplication app;std::ofstream csv(argv[3]);require(bool(csv),"csv");csv<<std::setprecision(17)<<"cohort,bits,mode,block,measured_blocks,pdc,deadline_us,mean_us,p99_us,worst_us,mean_deadline_percent,p99_deadline_percent,worst_deadline_percent,deadline_misses,thread_cpu_case_seconds,total_wall_including_warmup_seconds,checksum\n";if(detailed){callbackCsv.open(std::string(argv[3])+".callbacks.csv");callbackCsv<<std::setprecision(17)<<"cohort,block,index,wall_us,raw_thread_cpu_us,deadline_us\n";std::vector<double> emptyCpu(2048);for(auto&v:emptyCpu){double a=threadSeconds();double b=threadSeconds();v=(b-a)*1e9;}const auto cpuOverhead=stats(emptyCpu,1e9);std::cout<<"thread_info_pair_cpu_ns mean="<<cpuOverhead.mean<<" p99="<<cpuOverhead.p99<<" worst="<<cpuOverhead.worst<<" (not subtracted)\n";for(int block:{64,128,512})run<double>(*module,app,argv[1],block,1,csv,2048,true);}else for(int block:{64,128,512})for(unsigned mode:{0u,1u}){run<float>(*module,app,argv[1],block,mode,csv,2048);run<double>(*module,app,argv[1],block,mode,csv,2048);}std::cout<<"COMPLETE isolated component-only host. No editor, OS input, audio device, project changes or external measurement convolution. Wall timing includes scheduler effects; CPU case time includes minor host bookkeeping.\n";
}
