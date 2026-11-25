#pragma once
#include "checkpoint.hpp"
#include <optional>
#include <limits>
namespace fwc {
// Samples are taken BEFORE processCommands, in the same hook phase for both runs.
struct FastScan {
    bool enabled=false, validating=false, verified=false, loaded=false, aligned=false, creating=false;
    std::uintptr_t checkpoint=0;
    std::uint64_t captureCall=0, lastCompared=0, comparisons=0, restores=0, captureActual=0, totalCalls=0;
    std::int64_t tickOffset=0, baseReplayTickOffset=0;
    std::vector<std::pair<std::uint64_t,CheckSample>> baseline;
    void resetRestore() { loaded=false; aligned=false; tickOffset=0; lastCompared=0; comparisons=0; }
    void record(std::uint64_t call, CheckSample const& s) { baseline.emplace_back(call,s); }
    // Derive absolute logical call from the captured progress, not the cumulative tick index.
    std::optional<std::uint64_t> align(CheckSample const& a) {
        if (baseline.empty() || !a.valid()) return {};
        auto const& [call,e]=baseline.front();
        auto delta=std::int64_t(a.progress)-std::int64_t(e.progress);
        if (delta%2 || delta < -4 || delta > 0 || std::int64_t(call)+delta/2<1) return {};
        if(std::abs(a.time-e.time-delta/480.0)>1e-6) return {};
        tickOffset=std::int64_t(a.tick)-e.tick-delta;
        aligned=true;
        return static_cast<std::uint64_t>(std::int64_t(call)+delta/2);
    }
    bool compare(std::uint64_t call, CheckSample const& a) {
        if (baseline.empty() || !a.valid()) return false;
        if (call<baseline.front().first) return comparisons==0;
        auto i=call-baseline.front().first;
        if(i>=baseline.size() || baseline[i].first!=call) return false;
        if((comparisons==0 && i!=0) || (comparisons>0 && call!=lastCompared+1)) return false;
        CheckPair p{baseline[i].second,a};
        if(!p.countersMatch(tickOffset)||!p.flagsMatch()||p.positionError()>1e-3||p.velocityError()>1e-5) return false;
        lastCompared=call; ++comparisons; return true;
    }
};
}
