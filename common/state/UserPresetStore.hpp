#pragma once
#include "Preset.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <algorithm>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace just {
struct UserPreset {std::string id,name;SoundState state;std::vector<std::uint8_t> encodedSound;};
struct UserPresetList {std::vector<UserPreset> presets;std::size_t unreadable=0;std::string error;};
// Non-RT. A root is supplied by the platform shell. Construction/listing never
// creates directories. User mutations are atomic and serialized across instances.
// The fixed processor UID, not vendor/display name, isolates each plugin.
class UserPresetStore {
    std::filesystem::path directory;
    Uid uid;ParameterRegistry registry;
    static constexpr std::size_t maxNameBytes=240,maxFileBytes=maxStateBytes+maxNameBytes+8;
    struct Lock {
#ifdef _WIN32
        HANDLE handle=INVALID_HANDLE_VALUE;OVERLAPPED overlap{};
        explicit Lock(const std::filesystem::path& path){handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle!=INVALID_HANDLE_VALUE && !LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK,0,1,0,&overlap)){CloseHandle(handle);handle=INVALID_HANDLE_VALUE;}}
        explicit operator bool()const{return handle!=INVALID_HANDLE_VALUE;}
        ~Lock(){if(*this){UnlockFileEx(handle,0,1,0,&overlap);CloseHandle(handle);}}
#else
        int fd=-1;
        explicit Lock(const std::filesystem::path& path){fd=::open(path.c_str(),O_CREAT|O_RDWR|O_NOFOLLOW,0600);if(fd>=0 && ::flock(fd,LOCK_EX)){::close(fd);fd=-1;}}
        explicit operator bool()const{return fd>=0;}
        ~Lock(){if(fd>=0){::flock(fd,LOCK_UN);::close(fd);}}
#endif
    };
    static bool safeID(const std::string& id){return id.size()==32 && std::all_of(id.begin(),id.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});}
    std::filesystem::path file(const std::string& id)const{return directory/(id+".justpreset");}
    static bool regular(const std::filesystem::path& p){std::error_code e;return std::filesystem::is_regular_file(std::filesystem::symlink_status(p,e)) && !e;}
    static std::string randomID(){std::random_device random;std::string s;for(unsigned i=0;i<4;++i){const auto word=random();for(int n=7;n>=0;--n)s.push_back("0123456789abcdef"[(word>>(n*4))&15]);}return s;}
    bool prepare(std::string& error)const {
        if(directory.empty()){error="Preset directory is unavailable.";return false;}
        std::error_code e;std::filesystem::create_directories(directory,e);
        if(e){error="Cannot create preset directory: "+e.message();return false;}return true;
    }
    bool read(const std::string& id,UserPreset& out,std::string& error)const {
        if(!safeID(id) || !regular(file(id))){error="Preset is missing or is not a regular file.";return false;}
        std::error_code e;const auto size=std::filesystem::file_size(file(id),e);
        if(e || size<8+44 || size>maxFileBytes){error="Invalid preset file size.";return false;}
        std::ifstream stream(file(id),std::ios::binary);std::vector<std::uint8_t> bytes(size);
        if(!stream.read(reinterpret_cast<char*>(bytes.data()),size) || stream.peek()!=std::char_traits<char>::eof()){error="Cannot read preset.";return false;}
        if(std::memcmp(bytes.data(),"JUP1",4)){error="Unsupported preset format.";return false;}
        std::size_t nameLength=0;for(unsigned i=0;i<4;++i)nameLength|=std::size_t(bytes[4+i])<<(i*8);
        if(nameLength>maxNameBytes || 8+nameLength>=bytes.size()){error="Invalid preset name length.";return false;}
        UserPreset staged;staged.id=id;staged.name.assign(reinterpret_cast<const char*>(bytes.data()+8),nameLength);
        if(!validName(staged.name) || decodeState(bytes.data()+8+nameLength,bytes.size()-8-nameLength,uid,registry,staged.state)!=StateResult::ok || !validCompleteState(staged.state,uid,registry)){error="Preset name or sound state is invalid for this plugin.";return false;}
        staged.encodedSound.assign(bytes.begin()+8+nameLength,bytes.end());
        out=std::move(staged);return true;
    }
    bool write(const UserPreset& p,std::string& error)const {
        auto sound=p.encodedSound.empty()?encodeState(p.state,registry):p.encodedSound;std::vector<std::uint8_t> bytes{'J','U','P','1'};
        for(unsigned i=0;i<4;++i)bytes.push_back(std::uint8_t(p.name.size()>>(i*8)));
        bytes.insert(bytes.end(),p.name.begin(),p.name.end());bytes.insert(bytes.end(),sound.begin(),sound.end());
        auto temporary=directory/("."+randomID()+".tmp");
#ifdef _WIN32
        HANDLE h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE){error="Cannot create preset temporary file.";return false;}
        DWORD written=0;bool ok=WriteFile(h,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(h);CloseHandle(h);
        if(ok)ok=MoveFileExW(temporary.c_str(),file(p.id).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
#else
        int fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd<0){error="Cannot create preset temporary file.";return false;}
        std::size_t at=0;bool ok=true;while(at<bytes.size()){auto n=::write(fd,bytes.data()+at,bytes.size()-at);if(n<0 && errno==EINTR)continue;if(n<=0){ok=false;break;}at+=std::size_t(n);}
        if(ok)ok=::fsync(fd)==0;::close(fd);
        if(ok)ok=::rename(temporary.c_str(),file(p.id).c_str())==0;
#endif
        if(!ok){std::error_code e;std::filesystem::remove(temporary,e);error="Cannot atomically save preset.";return false;}
        return true;
    }
    bool uniqueName(const std::string& name,const std::string& except,std::string& error)const {
        const auto existing=list();if(!existing.error.empty()){error=existing.error;return false;}
        for(const auto& p:existing.presets)if(p.id!=except && p.name==name){error="A user preset already has this name.";return false;}return true;
    }
public:
    UserPresetStore(std::filesystem::path root,Uid identity,ParameterRegistry r):uid(identity),registry(r){
        if(!root.empty()){std::string key;for(auto word:uid.words)for(int n=7;n>=0;--n)key.push_back("0123456789abcdef"[(word>>(n*4))&15]);directory=root/key;}
    }
    static bool validName(const std::string& name){
        if(name.empty() || name.size()>maxNameBytes || name.front()==' ' || name.back()==' ')return false;
        // Strict UTF-8 and no control characters. Names never form filesystem paths.
        for(std::size_t i=0;i<name.size();){auto c=static_cast<unsigned char>(name[i++]);if(c<0x20 || c==0x7f)return false;if(c<0x80)continue;
            unsigned more=0,code=0,min=0;if(c>=0xc2 && c<=0xdf){more=1;code=c&31;min=0x80;}else if(c>=0xe0 && c<=0xef){more=2;code=c&15;min=0x800;}else if(c>=0xf0 && c<=0xf4){more=3;code=c&7;min=0x10000;}else return false;
            if(i+more>name.size())return false;while(more--){auto next=static_cast<unsigned char>(name[i++]);if((next&0xc0)!=0x80)return false;code=(code<<6)|(next&63);}if(code<min || code>0x10ffff || (code>=0xd800 && code<=0xdfff) || (code>=0x80 && code<=0x9f))return false;
        }return true;
    }
    const std::filesystem::path& path()const noexcept{return directory;}
    UserPresetList list()const {
        UserPresetList result;if(directory.empty()){result.error="Preset directory is unavailable.";return result;}
        std::error_code e;if(!std::filesystem::exists(directory,e)){if(e)result.error=e.message();return result;}
        std::filesystem::directory_iterator it(directory,e),end;if(e){result.error="Cannot list presets: "+e.message();return result;}
        unsigned visited=0;while(it!=end){if(++visited>4096){result.error="Preset directory contains too many entries.";break;}const auto p=it->path();
            if(p.extension()==".justpreset"){UserPreset preset;std::string error;if(read(p.stem().string(),preset,error))result.presets.push_back(std::move(preset));else ++result.unreadable;}
            it.increment(e);if(e){result.error="Cannot finish listing presets: "+e.message();break;}}
        std::sort(result.presets.begin(),result.presets.end(),[](const auto& a,const auto& b){return a.name==b.name?a.id<b.id:a.name<b.name;});return result;
    }
    bool load(const std::string& id,UserPreset& out,std::string& error)const {error.clear();return read(id,out,error);}
    bool save(const std::string& name,const SoundState& state,std::string& id,std::string& error)const {
        error.clear();if(!validName(name) || !validCompleteState(state,uid,registry)){error="Invalid name or sound state.";return false;}
        if(!prepare(error))return false;Lock lock(directory/".lock");if(!lock){error="Cannot lock preset directory.";return false;}
        if(!uniqueName(name,"",error))return false;
        try{UserPreset p;do{p.id=randomID();}while(std::filesystem::exists(file(p.id)));p.name=name;p.state=state;if(!write(p,error))return false;id=p.id;return true;}catch(const std::exception& e){error=e.what();return false;}
    }
    bool rename(const std::string& id,const std::string& name,std::string& error)const {
        error.clear();if(!safeID(id) || !validName(name)){error="Invalid preset selection or name.";return false;}
        if(!prepare(error))return false;Lock lock(directory/".lock");if(!lock){error="Cannot lock preset directory.";return false;}
        UserPreset p;if(!read(id,p,error) || !uniqueName(name,id,error))return false;p.name=name;
        try{return write(p,error);}catch(const std::exception& e){error=e.what();return false;}
    }
    bool remove(const std::string& id,std::string& error)const {
        error.clear();if(!safeID(id)){error="Invalid preset selection.";return false;}
        if(!prepare(error))return false;Lock lock(directory/".lock");if(!lock){error="Cannot lock preset directory.";return false;}
        UserPreset p;if(!read(id,p,error))return false;std::error_code e;const bool removed=std::filesystem::remove(file(id),e);
        if(!removed || e){error="Cannot delete preset.";return false;}return true;
    }
};
}
