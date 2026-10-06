#include "openrc/audio_read_ahead.hpp"

#include <functional>
#include <iostream>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void rejects(const std::function<void()>& call){try{call();}catch(const openrc::AudioReadAheadError&){return;}
    throw std::runtime_error("Invalid read-ahead was accepted");}
}
int main()try {
    openrc::AudioReadAheadPlayerV1 slow({56,4,12,4});
    unsigned frames=0;
    while(slow.before_frame(frames/2))++frames;
    check(frames==80 && slow.state().input_cursor==40 && slow.state().fetched_samples==56,
        "Slow input lost its prefetched end boundary");
    openrc::AudioReadAheadPlayerV1 fast({56,4,12,4});
    frames=0;while(fast.before_frame(frames*4))++frames;
    check(frames==13 && fast.state().input_cursor==52,
        "Fast input reused an invalid constant input-end cutoff");
    openrc::AudioReadAheadPlayerV1 held({56,4,12,4});
    for(unsigned i=0;i<100;++i)check(held.before_frame(0),"Held input ended without reaching its final refill");
    check(held.state().fetched_samples==16,"Held input filled past its explicit watermark");
    const auto before=held.state();
    rejects([&]{(void)held.before_frame(5);});
    check(held.state()==before,"Rejected cursor changed read-ahead ownership");
    openrc::AudioReadAheadPlayerV1 empty({4,4,12,4});
    check(!empty.before_frame(0) && empty.state().output_frames==0,"Initial final refill emitted an extra sample");
    rejects([]{openrc::AudioReadAheadPlayerV1 invalid({55,4,12,4});});
    rejects([]{openrc::AudioReadAheadPlayerV1 invalid({56,0,12,4});});
    rejects([]{openrc::AudioReadAheadPlayerV1 invalid({56,4,12,5});});
    std::cout<<"Audio read-ahead varying cursor/hold/end ownership/atomic rejection PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
