#include "windows_audio_program_bank.hpp"
#include "windows_pcm_stream.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

namespace openrc::runtime {
namespace {
constexpr std::size_t event_limit=256;
enum Command : std::uint32_t { admit_program=1,stop_program=2,stop_bank=3 };
struct Owner {
    std::uint64_t token=0,instance=0,admission_frame=0;
    std::uint32_t key=0;
    bool admitted=false,stop_requested=false;
};
struct Shared {
    mutable std::mutex mutex;
    std::vector<Owner> owners;
    std::vector<WindowsAudioProgramEventV1> events;
    AudioVoiceBankPlaybackStateV1 playback;
    std::size_t reserved_events=0;
    bool stop_acknowledged=false;
    HANDLE changed=nullptr;
    Shared() {
        owners.reserve(event_limit);events.reserve(event_limit);
        changed=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!changed)throw std::runtime_error("Cannot create audio bank acknowledgement event");
    }
    ~Shared(){CloseHandle(changed);}
    void signal() noexcept {SetEvent(changed);}
    // mutex is held; every owner reserved admission and terminal event slots.
    void event(const Owner& owner,WindowsAudioProgramEventKindV1 kind,std::uint64_t frame) {
        if(!reserved_events||events.size()>=event_limit)
            throw std::logic_error("Audio program event reservation was lost");
        events.push_back({owner.token,frame,owner.key,kind});--reserved_events;
    }
};
class Renderer final:public WindowsPcmWorkerRendererV1 {
public:
    Renderer(WindowsAudioProgramBankContentV1 content,std::uint64_t first,std::shared_ptr<Shared> shared):
        shared_(std::move(shared)),bank_(std::make_unique<AudioVoiceBankPlayerV1>(
            std::move(content.voices),std::move(content.program_resource_id),std::move(content.programs),
            std::move(content.streams),std::move(content.gains),first)) {
        shared_->playback=bank_->state();
    }
    void command(const WindowsPcmWorkerCommandV1& command) override {
        if(command.frame!=bank_->state().next_frame)
            throw std::logic_error("Audio program command and PCM clocks differ");
        if(command.kind==stop_bank) {
            bank_->stop_all();
            std::lock_guard lock(shared_->mutex);
            for(const auto& owner:shared_->owners) {
                if(!owner.admitted)--shared_->reserved_events; // no admission will follow
                shared_->event(owner,owner.admitted?WindowsAudioProgramEventKindV1::stopped:
                    WindowsAudioProgramEventKindV1::cancelled,command.frame);
            }
            shared_->owners.clear();shared_->playback=bank_->state();shared_->stop_acknowledged=true;
            shared_->signal();return;
        }
        std::lock_guard lock(shared_->mutex);
        const auto owner=std::find_if(shared_->owners.begin(),shared_->owners.end(),
            [&](const auto& value){return value.token==command.arguments[0];});
        // A queued stop may reach an owner whose natural completion was already
        // emitted. Its caller has not necessarily consumed that acknowledgement.
        if(command.kind==stop_program&&owner==shared_->owners.end())return;
        if(owner==shared_->owners.end())throw std::logic_error("Audio program command lost its accepted owner");
        if(command.kind==admit_program) {
            if(owner->admitted)throw std::logic_error("Audio program owner was admitted twice");
            owner->instance=bank_->admit(owner->key);owner->admitted=true;
            shared_->event(*owner,WindowsAudioProgramEventKindV1::admitted,command.frame);
        }else if(command.kind==stop_program) {
            if(!owner->admitted)throw std::logic_error("Audio program stop preceded its admission command");
            if(!owner->stop_requested&&bank_->instance_state(owner->instance))bank_->stop_instance(owner->instance);
            owner->stop_requested=true;
        }else throw std::logic_error("Unknown audio program worker command");
        collect_locked();shared_->playback=bank_->state();shared_->signal();
    }
    WindowsPcmWorkerRenderResultV1 render(std::uint64_t first,std::span<std::int16_t> output) override {
        if(first!=bank_->state().next_frame)throw std::logic_error("Audio program output clock moved unexpectedly");
        if(bank_->state().stopped)return {0,true};
        bank_->render(output);
        std::lock_guard lock(shared_->mutex);
        collect_locked();shared_->playback=bank_->state();shared_->signal();
        return {static_cast<std::uint32_t>(output.size()/2),false};
    }
    void stop() noexcept override {
        // Cancellation only destroys CPU owners. A successful normal stop must
        // already have executed the checked stop_bank command above.
        try {
            std::lock_guard lock(shared_->mutex);
            for(const auto& owner:shared_->owners) {
                if(!owner.admitted)--shared_->reserved_events;
                shared_->event(owner,WindowsAudioProgramEventKindV1::cancelled,shared_->playback.next_frame);
            }
            shared_->owners.clear();shared_->signal();
        }catch(...){} // worker destruction must still retire the native device
        bank_.reset();
    }
private:
    void collect_locked() {
        for(auto it=shared_->owners.begin();it!=shared_->owners.end();) {
            if(it->admitted&&!bank_->instance_state(it->instance)) {
                shared_->event(*it,it->stop_requested?WindowsAudioProgramEventKindV1::stopped:
                    WindowsAudioProgramEventKindV1::completed,bank_->state().next_frame);
                it=shared_->owners.erase(it);
            }else ++it;
        }
    }
    std::shared_ptr<Shared> shared_;
    std::unique_ptr<AudioVoiceBankPlayerV1> bank_;
};
}
struct WindowsAudioProgramBankV1::Implementation {
    std::shared_ptr<Shared> shared=std::make_shared<Shared>();
    std::unique_ptr<WindowsPcmWorkerV1> worker;
    std::vector<std::uint32_t> program_keys;
    std::uint64_t next_owner=1;
    bool started=false,closing=false,closed=false;
    Implementation(WindowsAudioProgramBankContentV1 content,std::uint64_t first,std::uint32_t buffers) {
        if(content.streams.empty()||content.streams.front().stream.output_sample_rate!=WindowsPcmStreamV1::sample_rate)
            throw AudioVoiceBankError("Windows program bank requires prepared 48000 Hz PCM");
        for(const auto& program:content.programs.programs)program_keys.push_back(program.key);
        const auto stride=content.voices.observation.frame_stride;
        worker=std::make_unique<WindowsPcmWorkerV1>(std::make_unique<Renderer>(std::move(content),first,shared),
            WindowsPcmWorkerOptionsV1{2,stride,buffers,64,first});
    }
    void check() {
        const auto state=worker->stats();
        if(state.failed||state.retirement_failed)worker->stop_and_join(); // propagates original failure
        if(state.worker_exited&&!closing)throw std::runtime_error("Audio program worker exited before stop acknowledgement");
    }
    void drain_commands() {
        check();
        for(const auto& result:worker->take_command_results())
            if(result.kind==WindowsPcmWorkerCommandResultKindV1::failed) {
                worker->stop_and_join();throw std::runtime_error("Audio program command failed");
            }
    }
};
WindowsAudioProgramBankV1::WindowsAudioProgramBankV1(WindowsAudioProgramBankContentV1 content,
    std::uint64_t first,std::uint32_t buffers):implementation_(std::make_unique<Implementation>(std::move(content),first,buffers)){}
WindowsAudioProgramBankV1::~WindowsAudioProgramBankV1()=default;
void WindowsAudioProgramBankV1::start() {
    auto& s=*implementation_;
    if(s.started||s.closing)throw std::logic_error("Audio program bank cannot start again");
    s.worker->start();s.started=true;
}
WindowsAudioProgramAdmissionV1 WindowsAudioProgramBankV1::queue(std::uint32_t key,std::uint64_t frame) {
    auto& s=*implementation_;
    if(s.closing)throw std::logic_error("Stopping audio program bank cannot accept a new owner");
    if(std::find(s.program_keys.begin(),s.program_keys.end(),key)==s.program_keys.end())
        throw AudioVoiceBankError("Audio program key is not admitted");
    s.drain_commands();
    std::lock_guard lock(s.shared->mutex);
    if(s.shared->events.size()+s.shared->reserved_events+2>event_limit||s.next_owner==UINT64_MAX)
        return {WindowsPcmWorkerAdmissionKindV1::full,0,0,s.worker->stats().earliest_command_frame};
    const auto result=s.worker->submit_command({frame,admit_program,{s.next_owner,0,0,0}});
    if(result.kind!=WindowsPcmWorkerAdmissionKindV1::accepted)return {result.kind,0,0,result.earliest_frame};
    s.shared->owners.push_back({s.next_owner,0,frame,key,false});s.shared->reserved_events+=2;
    return {result.kind,s.next_owner++,result.token,result.earliest_frame};
}
WindowsPcmWorkerAdmissionV1 WindowsAudioProgramBankV1::stop(std::uint64_t owner,std::uint64_t frame) {
    auto& s=*implementation_;
    if(s.closing||!owner||owner>=s.next_owner)throw std::logic_error("Audio program stop has no accepted owner");
    s.drain_commands();
    {std::lock_guard lock(s.shared->mutex);
        const auto found=std::find_if(s.shared->owners.begin(),s.shared->owners.end(),
            [&](const auto& value){return value.token==owner;});
        if(found!=s.shared->owners.end()&&frame<found->admission_frame)
            throw std::logic_error("Audio program stop cannot precede its queued admission frame");}
    return s.worker->submit_command({frame,stop_program,{owner,0,0,0}});
}
std::vector<WindowsAudioProgramEventV1> WindowsAudioProgramBankV1::take_events() {
    auto& s=*implementation_;s.drain_commands();
    std::lock_guard lock(s.shared->mutex);
    auto result=s.shared->events;s.shared->events.clear();return result;
}
WindowsAudioProgramBankStatsV1 WindowsAudioProgramBankV1::stats() const {
    const auto& s=*implementation_;WindowsAudioProgramBankStatsV1 result;
    {std::lock_guard lock(s.shared->mutex);
        result.playback=s.shared->playback;result.accepted_owners=s.shared->owners.size();
        result.pending_events=s.shared->events.size();result.stop_acknowledged=s.shared->stop_acknowledged;}
    result.worker=s.worker->stats();return result;
}
void WindowsAudioProgramBankV1::stop_all_and_wait() {
    auto& s=*implementation_;if(s.closed)return;s.closing=true;
    bool submitted=false;
    for(;;) {
        s.drain_commands();
        {std::lock_guard lock(s.shared->mutex);if(s.shared->stop_acknowledged)break;}
        const auto state=s.worker->stats();
        if(state.worker_exited)throw std::runtime_error("Audio bank ended without stop acknowledgement");
        if(!submitted) {
            const auto result=s.worker->submit_command({state.earliest_command_frame,stop_bank,{}});
            submitted=result.kind==WindowsPcmWorkerAdmissionKindV1::accepted;
        }
        if(!s.started){s.worker->start();s.started=true;}
        const auto wait=WaitForSingleObject(s.shared->changed,10);
        if(wait!=WAIT_OBJECT_0&&wait!=WAIT_TIMEOUT)throw std::runtime_error("Audio bank acknowledgement wait failed");
    }
    s.worker->stop_and_join();
    const auto result=stats();
    if(!result.stop_acknowledged||!result.playback.stopped||result.playback.instances||
        result.playback.pending_starts||result.playback.physical_voices||!result.worker.device_retired||!result.worker.joined)
        throw std::logic_error("Audio bank stop barrier retained an owner");
    s.closed=true;
}

} // namespace openrc::runtime
