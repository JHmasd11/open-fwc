#pragma once
#include "replay.hpp"
namespace fwc {
// Bounded single-tap experiment. A successful trial means survival to the
// same observation horizon, not a proof of a full-level completion.
struct Sweep {
    static constexpr int radius = 8;
    static constexpr int tail = 120;
    enum class Phase { Idle, Running, AwaitReset, Done, Aborted };
    struct Trial { int offset; std::string result; std::uint64_t observed; std::size_t applied; double error; };
    Phase phase = Phase::Idle;
    std::vector<Event> original;
    std::vector<int> offsets;
    std::vector<Trial> trials;
    std::size_t cursor = 0;
    std::uint64_t horizon = 0; // logic call, not zero-based tick
    std::string reason;
    bool running() const { return phase == Phase::Running; }
    bool pending() const { return phase == Phase::AwaitReset; }
    bool busy() const { return running() || pending(); }
    int offset() const { return cursor < offsets.size() ? offsets[cursor] : 0; }
    std::string begin(Attempt const& a, std::uint64_t observed) {
        *this = Sweep{};
        auto reject = replayRejection(a);
        if (!reject.empty()) return reject;
        if (a.events.size() != 2 || !a.events[0].down || a.events[1].down || a.events[0].player != 1 || a.events[1].player != 1 || a.events[0].button != 1 || a.events[1].button != 1)
            return "Record exactly one P1 jump";
        if (tickCandidate(a.events[0]) < radius || tickCandidate(a.events[1]) <= tickCandidate(a.events[0])) return "Jump later and hold at least one tick";
        horizon = a.events.back().logicCall + tail;
        if (observed < horizon) return "Wait 0.5s after releasing before restart";
        original = a.events;
        offsets.push_back(0);
        for (int i=1; i<=radius; ++i) { offsets.push_back(-i); offsets.push_back(i); }
        phase = Phase::Running;
        return {};
    }
    std::vector<Event> shifted() const {
        auto v=original;
        auto d=offset();
        for (auto& e : v) {
            e.logicCall = static_cast<std::uint64_t>(static_cast<std::int64_t>(e.logicCall)+d);
            e.progress = static_cast<unsigned>(2*e.logicCall);
            e.tickIndex = static_cast<int>(e.progress); e.commandIndex=e.progress;
            e.levelTime = (e.logicCall-1)/240.0;
        }
        return v;
    }
    void abort(std::string why) { phase=Phase::Aborted; reason=std::move(why); }
    void result(bool alive, std::uint64_t observed, std::size_t applied, double error) {
        if (!running()) return;
        bool valid = !alive || (observed>=horizon && applied==original.size());
        bool baseline = offset()==0;
        std::string outcome = !valid ? "invalid" : baseline && (!alive || error>0.1) ? "baseline-failed" : alive ? "survived" : "died";
        trials.push_back({offset(),outcome,observed,applied,error});
        if (!valid || outcome=="baseline-failed") { abort(outcome); return; }
        ++cursor;
        phase = cursor==offsets.size()?Phase::Done:Phase::AwaitReset;
    }
    bool next() { if (!pending()) return false; phase=Phase::Running; return true; }
    bool survived(int d) const { for(auto const& t:trials) if(t.offset==d) return t.result=="survived"; return false; }
    std::pair<int,int> window() const {
        if (!survived(0)) return {0,-1};
        int lo=0,hi=0;
        while(lo>-radius && survived(lo-1)) --lo;
        while(hi<radius && survived(hi+1)) ++hi;
        return {lo,hi};
    }
    std::string csv() const {
        std::ostringstream o; o.imbue(std::locale::classic()); o<<std::setprecision(17);
        o<<"offset_ticks,result,observed_logic,horizon_logic,inputs_applied,position_error_vs_original,original_down_tick,original_up_tick\n";
        for(auto const& t:trials) o<<t.offset<<','<<t.result<<','<<t.observed<<','<<horizon<<','<<t.applied<<','<<t.error<<','<<tickCandidate(original[0])<<','<<tickCandidate(original[1])<<'\n';
        return o.str();
    }
};
}
