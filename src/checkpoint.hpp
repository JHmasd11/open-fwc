#pragma once
#include "recorder.hpp"
#include <algorithm>

namespace fwc {
struct CheckSample {
    unsigned progress{}, command{};
    int tick{};
    double time{}, x{}, y{}, vy{}, speed{};
    unsigned flags{}; // grounded, upside-down, mode, direction
    bool valid() const {
        return std::isfinite(time) && std::isfinite(x) && std::isfinite(y) && std::isfinite(vy) && std::isfinite(speed);
    }
};
struct CheckPair {
    CheckSample expected, actual;
    double positionError() const { return std::hypot(expected.x-actual.x, expected.y-actual.y); }
    double velocityError() const { return std::abs(expected.vy-actual.vy); }
    std::int64_t tickOffset() const { return std::int64_t(actual.tick)-std::int64_t(expected.tick); }
    bool countersMatch(std::int64_t restoreOffset) const { return expected.progress==actual.progress && expected.command==actual.command && tickOffset()==restoreOffset && std::abs(expected.time-actual.time)<=1e-6; }
    bool flagsMatch() const { return expected.flags==actual.flags && std::abs(expected.speed-actual.speed)<=1e-5; }
};
struct CheckProbe {
    enum class Phase { Idle, Baseline, Ready, Comparing, Passed, Failed };
    static constexpr std::size_t target = 120;
    Phase phase = Phase::Idle;
    std::vector<CheckSample> baseline;
    std::vector<CheckPair> samples;
    std::string reason;
    double maxPosition{}, maxVelocity{};
    unsigned waitCalls{}, mismatches{};
    std::int64_t restoreTickOffset{};
    bool measuring() const { return phase==Phase::Baseline || phase==Phase::Comparing; }
    void begin() { *this=CheckProbe{}; phase=Phase::Baseline; }
    void fail(char const* why) { phase=Phase::Failed; reason=why; }
    bool restore() {
        if (baseline.size()!=target) { fail("baseline incomplete"); return false; }
        samples.clear(); reason.clear(); maxPosition=maxVelocity=0; waitCalls=mismatches=0; restoreTickOffset=0;
        phase=Phase::Comparing; return true;
    }
    void sample(CheckSample const& s) {
        if (!measuring()) return;
        if (!s.valid()) { fail("nonfinite state"); return; }
        if (phase==Phase::Baseline) {
            if (!baseline.empty() && s.progress<=baseline.back().progress) { fail("nonmonotonic baseline progress"); return; }
            baseline.push_back(s);
            if (baseline.size()==target) phase=Phase::Ready;
            return;
        }
        auto const& e=baseline[samples.size()];
        // Some native restore paths resume a step before the first baseline sample.
        // Never skip expected baseline samples or accept a shifted comparison.
        if (samples.empty() && s.progress<e.progress) {
            if (++waitCalls>480) fail("restore alignment timeout");
            return;
        }
        samples.push_back({e,s}); auto const& p=samples.back();
        // Native practice restores may retain a cumulative tick counter.
        // Align only at the first matching progress; require constant offset thereafter.
        if (samples.size()==1 && s.progress==e.progress) restoreTickOffset=p.tickOffset();
        maxPosition=std::max(maxPosition,p.positionError());
        maxVelocity=std::max(maxVelocity,p.velocityError());
        if (!p.countersMatch(restoreTickOffset) || !p.flagsMatch() || p.positionError()>1e-3 || p.velocityError()>1e-5) ++mismatches;
        if (s.progress!=e.progress) { fail("progress mismatch"); return; }
        if (samples.size()==target) {
            if (mismatches) fail("state diverged");
            else { phase=Phase::Passed; reason="sampled state matched"; }
        }
    }
    std::string csv() const {
        std::ostringstream o; o.imbue(std::locale::classic()); o<<std::setprecision(17);
        o<<"sequence,expected_progress,actual_progress,expected_tick,actual_tick,expected_command,actual_command,expected_time,actual_time,expected_x,expected_y,actual_x,actual_y,expected_vy,actual_vy,expected_speed,actual_speed,expected_flags,actual_flags,position_error,velocity_error,counters_match,flags_match,result,tick_offset,reference_tick_offset,relative_tick_error\n";
        for (std::size_t i=0;i<baseline.size();++i) {
            auto const& e=baseline[i]; o<<i+1<<','<<e.progress<<',';
            if (i<samples.size()) {
                auto const& p=samples[i]; auto const& a=p.actual;
                o<<a.progress<<','<<e.tick<<','<<a.tick<<','<<e.command<<','<<a.command<<','<<e.time<<','<<a.time<<','<<e.x<<','<<e.y<<','<<a.x<<','<<a.y<<','<<e.vy<<','<<a.vy<<','<<e.speed<<','<<a.speed<<','<<e.flags<<','<<a.flags<<','<<p.positionError()<<','<<p.velocityError()<<','<<p.countersMatch(restoreTickOffset)<<','<<p.flagsMatch()<<','<<csvQuote(reason)<<','<<p.tickOffset()<<','<<restoreTickOffset<<','<<(p.tickOffset()-restoreTickOffset)<<'\n';
            } else {
                o<<','<<e.tick<<",,"<<e.command<<",,"<<e.time<<",,"<<e.x<<','<<e.y<<",,,"<<e.vy<<",,"<<e.speed<<",,"<<e.flags<<",,,,,,"<<csvQuote(reason)<<",,,\n";
            }
        }
        return o.str();
    }
};
}
