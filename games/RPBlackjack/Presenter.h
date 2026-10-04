// The presentation layer of the play screen.
//
// PPOT redrew the hands straight from the game state each frame, one card
// popping in every 15 frames. Here Round (the rules) reports events and the
// presenter turns them into motion: cards fly out of the shoe and flip, hands
// re-centre, chips fly to and from the betting circle, payouts stream out of
// the dealer's rack, the purse rolls, the dealer talks, blinks and watches the
// cards. Round waits on busy() so the rules never outrun the show.
#pragma once
#include <stdint.h>

class Round;

namespace present {

void reset(const Round &r);
void onEvents(Round &r);            // drain the round's event queue
void update(const Round &r);        // advance animations one frame
bool busy();
// render() redraws the wall and felt bands that need it and returns true if
// it drew anything; overlay() then draws what moves (chips in flight,
// particles, banners). rowsMoving() says whether anything moving touched
// rows a..b this frame or last (the action bar uses it to know when to redraw).
bool render(const Round &r, uint32_t frame);
bool rowsMoving(int a, int b);
void overlay();
void dismissBubble();
// The dealer's speech bubble (beside him on the wall) showing the first
// `typed` characters of text; '\n' separates lines of up to 12 characters.
void speechBubble(const char *text, int typed);
void invalidate();                  // redraw everything next frame (overlays, screen changes)
int32_t shownPurse();

}  // namespace present
