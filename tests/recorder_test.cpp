#include "../src/recorder.hpp"
#include <cassert>
#include <iostream>
#include <algorithm>
int main() {
    fwc::Attempt a;
    fwc::Event e{};
    e.progress = 120; e.tickIndex = 3; e.commandIndex = 2; e.logicCall = 121;
    e.player = 1; e.button = 1; e.down = true; e.x = 12.5f; e.y = -3.f;
    a.push(e);
    e.down = false; e.player = 2; e.progress = 125;
    a.push(e);
    assert(a.presses == 1 && a.releases == 1 && a.events.size() == 2);
    auto csv = a.csv();
    assert(csv.find("1,120,3,2,121,0,0,0,0,0,1,1,1,12.5,-3,0\n") != std::string::npos);
    assert(csv.find("2,125,3,2,121,0,0,0,0,0,2,1,0,12.5,-3,0\n") != std::string::npos);
    assert(std::count(csv.begin(), csv.end(), '\n') == 3);
    a.clear();
    assert(a.events.empty() && a.presses == 0 && a.releases == 0);
    std::cout << "Recorder and CSV tests passed\n";
}
