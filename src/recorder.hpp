#pragma once
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

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
struct Attempt {
    std::vector<Event> events;
    std::size_t presses{}, releases{};
    void clear() { events.clear(); presses = releases = 0; }
    void push(Event e) {
        events.push_back(e);
        if (e.down) ++presses; else ++releases;
    }
    std::string csv() const {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "sequence,progress_raw,tick_index_raw,command_index_raw,logic_call,inside_logic,half_tick,last_tick,level_time_s,total_time_s,player,button,down,x,y,dead\n";
        out << std::setprecision(17);
        for (std::size_t i = 0; i < events.size(); ++i) {
            auto const& e = events[i];
            out << i + 1 << ',' << e.progress << ',' << e.tickIndex << ',' << e.commandIndex << ',' << e.logicCall << ','
                << e.insideLogic << ',' << e.halfTick << ',' << e.lastTick << ',' << e.levelTime << ',' << e.totalTime << ','
                << e.player << ',' << e.button << ',' << e.down << ',' << e.x << ',' << e.y << ',' << e.dead << '\n';
        }
        return out.str();
    }
};
}
