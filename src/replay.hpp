#pragma once
#include "recorder.hpp"
#include <algorithm>
#include <optional>

namespace fwc {
// A conservative replay only accepts complete, ordered, observed full-tick transitions.
inline std::string replayRejection(Attempt const& attempt) {
    if (attempt.events.empty()) return "no recorded inputs";
    if (!attempt.held.empty()) return "release all buttons before restart";
    std::set<std::pair<int, int>> held;
    std::int64_t previous = -1;
    for (auto const& e : attempt.events) {
        auto tick = tickCandidate(e);
        if (tick < 0 || !e.insideQueue || tick < previous) return "unverified input phase or timing";
        if (e.dead || !std::isfinite(e.x) || !std::isfinite(e.y)) return "invalid player state";
        if (e.player < 1 || e.player > 2 || e.button < 1 || e.button > 3) return "unknown input";
        auto key = std::pair{e.player, e.button};
        if (e.down ? !held.insert(key).second : !held.erase(key)) return "inconsistent button transitions";
        previous = tick;
    }
    return held.empty() ? std::string{} : "unbalanced input";
}
struct ReplaySample {
    Event expected, actual;
    double error() const { return std::hypot(double(actual.x) - expected.x, double(actual.y) - expected.y); }
};
struct Replay {
    enum class Phase { Idle, Running, Finished, Failed };
    Phase phase = Phase::Idle;
    std::vector<Event> events;
    std::vector<ReplaySample> samples;
    std::set<std::pair<int, int>> held;
    std::size_t cursor = 0;
    std::string message;
    double maxError = 0;
    bool running() const { return phase == Phase::Running; }
    void begin(std::vector<Event> input) { *this = Replay{}; events = std::move(input); phase = Phase::Running; }
    Event const* next() const { return cursor < events.size() ? &events[cursor] : nullptr; }
    bool matches(Event const& a) const {
        auto e = next();
        return e && a.insideQueue && a.insideLogic && a.halfTick == e->halfTick &&
            a.logicCall == e->logicCall && a.progress == e->progress && a.tickIndex == e->tickIndex &&
            a.commandIndex == e->commandIndex && std::isfinite(a.levelTime) &&
            std::abs(a.levelTime-e->levelTime) <= 1e-6;
    }
    void applied(Event const& a) {
        auto const& e = events[cursor++];
        samples.push_back({e, a});
        maxError = std::max(maxError, samples.back().error());
        auto key = std::pair{e.player, e.button};
        if (e.down) held.insert(key); else held.erase(key);
    }
    std::string csv(std::string const& reason) const {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "sequence,expected_tick,actual_tick,expected_raw,actual_raw,player,button,down,expected_x,expected_y,actual_x,actual_y,error,expected_logic,actual_logic,result\n";
        out << std::setprecision(17);
        for (std::size_t i=0; i<samples.size(); ++i) {
            auto const& s = samples[i]; auto const& e = s.expected; auto const& a = s.actual;
            out << i+1 << ',' << tickCandidate(e) << ',' << tickCandidate(a) << ',' << e.progress << ',' << a.progress << ','
                << e.player << ',' << e.button << ',' << e.down << ',' << e.x << ',' << e.y << ',' << a.x << ',' << a.y << ','
                << s.error() << ',' << e.logicCall << ',' << a.logicCall << ',' << csvQuote(reason) << '\n';
        }
        return out.str();
    }
};
}
