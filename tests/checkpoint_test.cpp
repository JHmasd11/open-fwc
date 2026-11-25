#include "../src/checkpoint.hpp"
#include <cassert>
#include <iostream>
fwc::CheckSample at(unsigned i) { return {2*i,2*i,int(2*i),i/240.,double(i),20.,0.,1.,1}; }
int main() {
    using P=fwc::CheckProbe; using Phase=P::Phase;
    P p; assert(!p.restore()); p.begin();
    for(unsigned i=100;i<100+P::target;++i) p.sample(at(i));
    assert(p.phase==Phase::Ready && p.baseline.size()==P::target);
    assert(p.restore()); p.sample(at(99)); assert(p.samples.empty());
    for(unsigned i=100;i<100+P::target;++i) p.sample(at(i));
    assert(p.phase==Phase::Passed && p.maxPosition==0 && p.maxVelocity==0 && p.mismatches==0);
    assert(p.restore()); auto s=at(100); s.y+=3; s.vy+=2; s.flags^=2; p.sample(s);
    for(unsigned i=101;i<100+P::target;++i) p.sample(at(i));
    assert(p.phase==Phase::Failed && p.maxPosition==3 && p.maxVelocity==2 && p.mismatches==1);
    assert(p.restore()); p.sample(at(101)); assert(p.phase==Phase::Failed && p.reason=="progress mismatch");
    // Constant offsets observed in two native restores must pass independently.
    for (int offset : {3214,6428}) {
        assert(p.restore());
        for(unsigned i=100;i<100+P::target;++i) { auto shifted=at(i); shifted.tick+=offset; p.sample(shifted); }
        assert(p.phase==Phase::Passed && p.restoreTickOffset==offset && p.mismatches==0);
    }
    assert(p.restore()); s=at(100); s.tick+=3214; p.sample(s);
    s=at(101); s.tick+=3215; p.sample(s); assert(p.mismatches==1);
    for(unsigned i=102;i<100+P::target;++i) { s=at(i); s.tick+=3214; p.sample(s); }
    assert(p.phase==Phase::Failed && p.reason=="state diverged");
    assert(p.restore()); s=at(100); s.command++; p.sample(s); assert(p.mismatches==1);
    assert(p.restore()); for(int i=0;i<481;++i) p.sample(at(99)); assert(p.phase==Phase::Failed);
    p.begin(); p.sample(at(100)); p.sample(at(100)); assert(p.phase==Phase::Failed);
    p.begin(); s=at(100); s.x=INFINITY; p.sample(s); assert(p.phase==Phase::Failed);
    p.begin(); p.sample(at(100)); assert(!p.restore());
    assert(p.csv().find("baseline incomplete")!=std::string::npos);
    std::cout<<"Checkpoint probe tests passed\n";
}
