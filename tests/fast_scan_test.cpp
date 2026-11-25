#include "../src/fast_scan.hpp"
#include "../src/replay.hpp"
#include <cassert>
#include <iostream>
fwc::CheckSample sample(int c,int off=0) {
    fwc::CheckSample s; s.progress=s.command=2*(c-1);s.tick=2*c+off;
    s.time=(c-1)/240.;s.x=c*1.2;s.y=105;s.speed=.9;s.flags=1;return s;
}
int main(){
    fwc::FastScan f;
    for(int c=100;c<240;++c)f.record(c,sample(c));
    assert(f.align(sample(99,3214))==99);assert(f.tickOffset==3214);
    assert(f.compare(99,sample(99,3214)));
    for(int c=100;c<240;++c)assert(f.compare(c,sample(c,3214)));
    assert(f.comparisons==140);f.resetRestore();
    assert(f.align(sample(100,6428))==100);assert(f.compare(100,sample(100,6428)));
    auto bad=sample(101,6428);bad.y+=1;assert(!f.compare(101,bad));
    bad=sample(101,6429);assert(!f.compare(101,bad));
    assert(!f.compare(102,sample(102,6428))); // no missing ticks allowed
    f.resetRestore();assert(!f.align(sample(101,0)));assert(!f.align(sample(95,0)));
    fwc::Event e;e.player=1;e.button=1;e.down=true;e.insideLogic=e.insideQueue=true;e.logicCall=120;e.progress=e.commandIndex=240;e.tickIndex=240;e.levelTime=119/240.;
    fwc::Replay r;r.begin({e});r.tickOffset=3214;auto actual=e;actual.tickIndex+=3214;
    assert(r.matches(actual));assert(r.confirm(actual,false,true));assert(r.csv("done").find("119,119,240,240")!=std::string::npos);
    std::cout<<"Fast checkpoint alignment, sampled-state validation, gap rejection and replay tick-offset tests passed\n";
}
