#include "../src/sweep.hpp"
#include <cassert>
#include <iostream>
fwc::Event at(int t, bool down) {
    fwc::Event e; e.player=1;e.button=1;e.down=down;e.insideLogic=e.insideQueue=true;
    e.logicCall=t+1;e.progress=e.commandIndex=2*(t+1);e.tickIndex=e.progress;e.levelTime=t/240.0;return e;
}
int main(){
    fwc::Attempt a; a.push(at(100,true));a.push(at(130,false));
    fwc::Sweep s; assert(!s.begin(a,200).empty());
    assert(s.begin(a,260).empty());assert(s.offsets.size()==17&&s.horizon==251);
    s.result(true,251,2,0);assert(s.pending());assert(s.next());assert(s.offset()==-1);
    auto v=s.shifted();assert(fwc::tickCandidate(v[0])==99&&fwc::tickCandidate(v[1])==129);
    assert(v[1].logicCall-v[0].logicCall==30);
    while(s.running()) {auto d=s.offset(); s.result(d>=-2&&d<=3,251,2,5);s.next();}
    assert(s.phase==fwc::Sweep::Phase::Done && s.trials.size()==17);
    assert((s.window()==std::pair{-2,3}));
    assert(s.csv().find("-3,died")!=std::string::npos);
    assert(s.begin(a,260).empty());s.result(true,251,2,0.2);assert(s.phase==fwc::Sweep::Phase::Aborted);
    assert(s.begin(a,260).empty());s.result(true,250,2,0);assert(s.phase==fwc::Sweep::Phase::Aborted);
    assert(s.begin(a,260).empty());s.result(false,99,0,0);assert(s.phase==fwc::Sweep::Phase::Aborted);
    assert(s.begin(a,260).empty());s.abort("paused");assert(!s.next());
    a.push(at(150,true));a.push(at(180,false));assert(!s.begin(a,400).empty());
    std::cout<<"Sweep scheduling, bounds, duration, baseline, survival, CSV and cancellation tests passed\n";
}
