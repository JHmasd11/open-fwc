#include "../src/recorder.hpp"
#include <cassert>
#include <iostream>
#include <algorithm>
int main() {
    fwc::Attempt a;
    fwc::Event e{};
    e.player = 1; e.button = 1;
    assert(!a.push(e)); // startup release
    e.down = true; e.progress = 66; e.tickIndex = 66; e.commandIndex = 66;
    e.logicCall = 33; e.insideLogic = true; e.levelTime = 32.0 / 240; e.x = 12.5f;
    assert(fwc::tickCandidate(e) == 32);
    assert(a.push(e)); assert(!a.push(e));
    e.player = 2; assert(a.push(e)); // independent P2
    e.button = 2; assert(a.push(e)); // independent button
    e.down = false; assert(a.push(e)); assert(!a.push(e));
    e.button = 1; assert(a.push(e));
    e.player = 1; assert(a.push(e));
    assert(a.presses == 3 && a.releases == 3 && a.events.size() == 6);
    auto csv = a.csv("test, \"quoted\"", 42, "reset");
    assert(csv.find("32,42,\"test, \"\"quoted\"\"\",\"reset\",input,0\n") != std::string::npos);
    assert(std::count(csv.begin(), csv.end(), '\n') == 7);
    e.halfTick = true; assert(fwc::tickCandidate(e) == -1);
    e.halfTick = false; e.levelTime = 1; assert(fwc::tickCandidate(e) == -1);
    a.clear(); assert(a.events.empty() && a.presses == 0 && a.releases == 0 && a.held.empty());
    assert(!a.push(e)); e.down = true; assert(a.push(e));
    // Resume-time release is explicit and cannot masquerade as a precise gameplay tick.
    e.down = false; e.source = fwc::Event::Source::PauseSync;
    e.progress = 66; e.tickIndex = 66; e.commandIndex = 66;
    e.logicCall = 33; e.insideLogic = true; e.levelTime = 32.0 / 240;
    assert(fwc::tickCandidate(e) == -1);
    assert(a.push(e)); assert(!a.push(e));
    assert(a.csv().find(",pause-sync,0\n") != std::string::npos);
    e.down = true; e.source = fwc::Event::Source::Input;
    assert(a.push(e)); assert(fwc::tickCandidate(e) == 32);
    fwc::ResetGate gate; assert(gate.begin()); assert(!gate.begin()); assert(!gate.end()); assert(gate.end()); assert(!gate.end());
    std::cout << "Recorder, independent inputs, tick candidate, reset gate and CSV tests passed\n";
}
