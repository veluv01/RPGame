#include <cassert>
#include <cstdio>
#include <rpgame/Input.h>
static uint32_t nowUs;
static uint8_t keys;
uint32_t micros(){return nowUs;}
uint32_t millis(){return nowUs/1000;}
uint8_t rpgame_readButtons(){return keys;}
int main(){
    RPGame g;g.startExits=false;
    keys=A_BUTTON;g.boot();assert(g.pressed(A_BUTTON));
    g.pollButtons();assert(!g.justPressed(A_BUTTON));
    keys=0;assert(g.pressed(A_BUTTON)); // queries retain the frame snapshot
    g.pollButtons();assert(g.justReleased(A_BUTTON));
    g.injected=B_BUTTON;g.pollButtons();assert(g.justPressed(B_BUTTON));
    assert(g.repeat(B_BUTTON));
    for(int i=0;i<17;i++){g.pollButtons();assert(!g.repeat(B_BUTTON));}
    g.pollButtons();assert(!g.repeat(B_BUTTON));
    g.pollButtons();assert(!g.repeat(B_BUTTON));
    g.pollButtons();assert(!g.repeat(B_BUTTON));
    g.pollButtons();assert(!g.repeat(B_BUTTON));
    g.pollButtons();assert(g.repeat(B_BUTTON));
    assert(g.repeat(B_BUTTON,0,0)); // a zero rate is bounded
    g.injected=0;g.pollButtons();assert(!g.repeat(B_BUTTON));
    g.lockstep=3;for(int i=0;i<3;i++)assert(g.nextFrame());assert(!g.nextFrame());
    g.lockstep=-1;nowUs=0xfffffff0;g.setFrameRate(60);
    assert(!g.nextFrame());nowUs+=16666;assert(g.nextFrame());
    assert(!g.nextFrame());nowUs+=1000000;assert(g.nextFrame());assert(!g.nextFrame());
    g.setFrameRate(0);nowUs+=1000000;assert(g.nextFrame());
    assert(!g.everyXFrames(0));assert(g.getFrameCount(0)==0);
    puts("input: snapshots, injection, repeat, lockstep, timer wrap and resync: PASS");
}
