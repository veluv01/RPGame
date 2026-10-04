// The words (Text.h): table names, each table's rules as the dealer
// tells them, and his remarks.
#include "Text.h"

const char *const MODE_NAME[MODE_COUNT] = {
    "CLASSIC", "BLITZ", "MISERE", "ALL X", "VANISH", "GOBBLE", "WILD", "DARK", "COIN FLIP", "AUCTION",
    "BIG 5", "WRAP", "MINES", "DROP 4", "ULTIMATE", "THE 99",
};

const char *const MODE_RULES[MODE_COUNT] = {
    "Three in a row.\nYou know this\none. Surely.",
    "60 seconds.\nBoards keep\ncoming. 3 secs\na move! 3 wins\nto profit.",
    "Three in a row\nLOSES. Try not\nto succeed.",
    "We both play X.\nMake three in a\nrow and you LOSE.",
    "3 marks each.\nPlace a 4th and\nyour oldest one\nvanishes.",
    "Big chips cover\nsmaller ones.\nA: place\nB: change size",
    "Play X or O.\nA: X    B: O\nFinish any line\nand it's yours.",
    "My marks are\nhidden. Bump one\nand it shows.\nI can see fine.",
    "A coin toss\ndecides who\nmoves. Every\nsingle time.",
    "Bid chips for\nevery move.\nThe winner pays\nthe loser.",
    "A 5x5 felt.\nFour in a row\nwins.",
    "5x5, four in a\nrow, and lines\nwrap round the\nedges.",
    "5x5, four in a\nrow. Four hidden\nmines eat your\nmove. Good luck.",
    "Marks drop down\nthe column.\nFour in a row\nwins.",
    "9 boards. Your\nsquare picks my\nnext board. Win\n3 boards in a\nrow.",
    "99 squares.\nFive in a row.\nTake your time.",
};

const char *const QUIPS[QUIP_ANY + QUIP_ANY_N] = {
    "THE CENTRE. BOLD.",
    "A CORNER. I SEE.",
    "AN EDGE? BRAVE.",
    "I MOVED. SOMEWHERE.",
    "BEGINNER'S LUCK.", "I LET YOU HAVE THAT.",
    "THE HOUSE THANKS YOU.", "BETTER LUCK NEXT TIME.",
    "NOBODY WINS. A CLASSIC.", "THE CAT TAKES IT.",
    "INTERESTING...",
    "ARE YOU SURE?",
    "THE HOUSE NOTES IT.",
    "A PROFESSIONAL, EH?",
    "SECURITY IS WATCHING.",
    "HIGH STAKES NOUGHTS.",
};
