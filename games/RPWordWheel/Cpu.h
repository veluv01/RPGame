// The CPU contestants. They know the answer and are handicapped: each
// persona finds a letter that is really there only some of the time, buys
// vowels by temperament, and solves once enough of the board is turned
// (give or take the nerve it drew for this puzzle). Pure logic; every draw
// comes from the show's generator, so an episode replays exactly.
//
//   ACE   sharp: usually calls the best consonant, solves early
//   DOT   steady: buys every vowel she can afford, never gambles
//   BUZZ  rookie: guesses, solves late and sometimes wrong, flips anything
#pragma once
#include <stdint.h>

class Show;

namespace cpu {

void    newPuzzle(Show &s);         // draw each CPU's nerve and toss-up buzz point
uint8_t turn(Show &s);              // an Act: spin, buy a vowel, solve, play the wild card
char    letter(Show &s);            // for the picker's mode (also the bonus round's picks)
bool    solveRight(Show &s);        // does the solve they announced come out right?
bool    flip(Show &s);              // the mystery wedge
bool    finalSolve(Show &s);        // after a hit on the final spin: try to solve?
bool    tossRight(Show &s, uint8_t p);
bool    bonusWin(Show &s);

}  // namespace cpu
