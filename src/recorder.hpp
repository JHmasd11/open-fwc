#pragma once
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <utility>
#include <cmath>

namespace fwc {
struct Event {
    unsigned progress{};
    int tickIndex{};
    unsigned commandIndex{};
    std::uint64_t logicCall{};
    bool insideLogic{};
    bool halfTick{};
    bool lastTick{};
    double levelTime{};
    double totalTime{};
    int player{};
    int button{};
    bool down{};
    float x{}, y{};
    bool dead{};
};
inline std::string csvQuote(std::string const& value) {
    std::string out = "\"";
    for (char c : value) { if (c == '"') out += '"'; out += c; }
    return out + '"';
}
// Only emit observed 240 TPS candidates that match all raw counters and time.
inline std::int64_t tickCandidate(Event const& e) {
    if (e.logicCall == 0 || !e.insideLogic || e.halfTick || e.tickIndex < 0 ||
        e.progress != static_cast<unsigned>(e.tickIndex) || e.progress != e.commandIndex ||
        e.progress % 2 != 0 || e.progress / 2 != e.logicCall ||
        !std::isfinite(e.levelTime) || std::abs(e.levelTime - (e.logicCall - 1) / 240.0) > 1e-6) return -1;
    return static_cast<std::int64_t>(e.logicCall - 1);
}
struct ResetGate {
    unsigned depth{};
    bool begin() { return depth++ == 0; }
    bool end() { return depth && --depth == 0; }
};
struct Attempt {
    std::vector<Event> events;
    std::size_t presses{}, releases{};
    std::set<std::pair<int, int>> held;
    void clear() { events.clear(); held.clear(); presses = releases = 0; }
    bool push(Event e) {
        auto key = std::pair{e.player, e.button};
        if (e.down) { if (!held.insert(key).second) return false; }
        else { if (!held.erase(key)) return false; }
        events.push_back(e);
        if (e.down) ++presses; else ++releases;
        return true;
    }
    std::string csv(std::string const& levelName = {}, int levelId = 0, std::string const& reason = {}) const {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "sequence,progress_raw,tick_index_raw,command_index_raw,logic_call,inside_logic,half_tick,last_tick,level_time_s,total_time_s,player,button,down,x,y,dead,tick_240_candidate,level_id,level_name,end_reason\n";
        out << std::setprecision(17);
        for (std::size_t i = 0; i < events.size(); ++i) {
            auto const& e = events[i];
            out << i + 1 << ',' << e.progress << ',' << e.tickIndex << ',' << e.commandIndex << ',' << e.logicCall << ','
                << e.insideLogic << ',' << e.halfTick << ',' << e.lastTick << ',' << e.levelTime << ',' << e.totalTime << ','
                << e.player << ',' << e.button << ',' << e.down << ',' << e.x << ',' << e.y << ',' << e.dead << ',' << tickCandidate(e) << ',' << levelId << ',' << csvQuote(levelName) << ',' << csvQuote(reason) << '\n';
        }
        return out.str();
    }
};
}
