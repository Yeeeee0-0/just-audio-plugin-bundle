#pragma once
#include "../state/State.hpp"
#include <atomic>
namespace just {
template<class T,std::size_t Capacity> class SpscQueue {
    static_assert(std::is_trivially_copyable<T>::value,"Only fixed POD snapshots");
    static_assert(Capacity>1 && Capacity<0xFFFFFFFFu,"Queue size must fit its index");
    std::array<T,Capacity> entries{};
    std::atomic<std::uint32_t> writeIndex{0},readIndex{0};
public:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,"RT queue requires lock-free atomics");
    bool push(const T& value) noexcept {
        const auto w=writeIndex.load(std::memory_order_relaxed);
        const auto next=static_cast<std::uint32_t>((w+1)%Capacity);
        if(next==readIndex.load(std::memory_order_acquire))return false;
        entries[w]=value;writeIndex.store(next,std::memory_order_release);return true;
    }
    bool pop(T& value) noexcept {
        auto r=readIndex.load(std::memory_order_relaxed);
        if(r==writeIndex.load(std::memory_order_acquire))return false;
        value=entries[r];readIndex.store((r+1)%Capacity,std::memory_order_release);return true;
    }
};
// Single writer per instance. Readers make bounded attempts; every storage word is
// atomic, so a failed read has no C++ data race. No pointer publication/reclamation.
template<class T> class AtomicSnapshot {
    static_assert(std::is_trivially_copyable<T>::value,"Snapshot must be POD");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"64-bit target required");
    static constexpr std::size_t words=(sizeof(T)+7)/8;
    std::array<std::atomic<std::uint64_t>,words> storage{};
    std::atomic<std::uint32_t> sequence{0};
public:
    void publish(const T& value) noexcept {
        sequence.fetch_add(1,std::memory_order_seq_cst);
        std::array<std::uint64_t,words> raw{};std::memcpy(raw.data(),&value,sizeof(T));
        for(std::size_t i=0;i<words;++i)storage[i].store(raw[i],std::memory_order_seq_cst);
        sequence.fetch_add(1,std::memory_order_seq_cst);
    }
    bool read(T& value) const noexcept {
        for(int attempt=0;attempt<3;++attempt) {
            auto before=sequence.load(std::memory_order_seq_cst);if(before&1)continue;
            std::array<std::uint64_t,words> raw{};
            for(std::size_t i=0;i<words;++i)raw[i]=storage[i].load(std::memory_order_seq_cst);
            auto after=sequence.load(std::memory_order_seq_cst);
            if(before==after){std::memcpy(&value,raw.data(),sizeof(T));return true;}
        }
        return false;
    }
};
inline double finiteOr(double value,double fallback=0) noexcept {return std::isfinite(value)?value:fallback;}
class LinearSmoother {
    double value=0,target=0,increment=0;std::uint32_t remaining=0;
public:
    void reset(double initial) noexcept {value=target=finiteOr(initial);increment=0;remaining=0;}
    void setTarget(double next,std::uint32_t samples) noexcept {
        target=finiteOr(next,value);remaining=samples;
        if(!samples)value=target;else increment=(target-value)/samples;
    }
    double tick() noexcept {if(remaining){value+=increment;if(!--remaining)value=target;}return value;}
    double effective() const noexcept{return value;}
};
}
