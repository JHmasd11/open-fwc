#include "../src/replay.hpp"
#include <cassert>
#include <iostream>
fwc::Event at(int tick, bool down) {
    fwc::Event e{}; e.player=1; e.button=1; e.down=down;
    e.logicCall=tick+1; e.progress=e.tickIndex=e.commandIndex=2*(tick+1);
    e.levelTime=tick/240.0; e.insideLogic=e.insideQueue=true; e.x=10; e.y=20;
    return e;
}
int main() {
    fwc::Attempt a;
    assert(!fwc::replayRejection(a).empty());
    assert(a.push(at(20,true))); assert(!fwc::replayRejection(a).empty());
    assert(a.push(at(40,false))); assert(fwc::replayRejection(a).empty());
    auto invalid=a; invalid.events[0].insideQueue=false; assert(!fwc::replayRejection(invalid).empty());
    invalid=a; invalid.events[1].source=fwc::Event::Source::PauseSync; assert(!fwc::replayRejection(invalid).empty());
    invalid=a; invalid.events[0].dead=true; assert(!fwc::replayRejection(invalid).empty());
    invalid=a; invalid.events[0].button=0; assert(!fwc::replayRejection(invalid).empty());
    invalid=a; invalid.events[1]=at(19,false); assert(!fwc::replayRejection(invalid).empty());
    fwc::Replay r; r.begin(a.events); assert(r.running()); assert(r.next()->down);
    assert(!r.matches(at(19,true))); assert(!r.matches(at(21,true))); assert(r.matches(at(20,true)));
    auto wrong=at(20,true); wrong.commandIndex++; assert(!r.matches(wrong));
    auto actual=at(20,true); actual.x=13; actual.y=24; r.applied(actual);
    assert(r.cursor==1 && r.held.size()==1 && r.maxError==5);
    assert(r.matches(at(40,false))); r.applied(at(40,false));
    assert(r.held.empty() && r.next()==nullptr && r.cursor==2);
    assert(r.csv("test").find("20,20,42,42,1,1,1,10,20,13,24,5")!=std::string::npos);
    r.begin(a.events); assert(r.cursor==0 && r.samples.empty() && r.maxError==0);
    std::cout << "Replay validation, timing match, position error, CSV and reset tests passed\n";
}
