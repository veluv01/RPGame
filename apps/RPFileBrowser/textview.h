#pragma once
#include "rpgame.h"

/* Opens `path` and runs its own input loop until the user backs out. Streams
 * from the card: nothing larger than a 256-byte window is ever held in RAM,
 * so file size is irrelevant. Uses the scratch arena, so the caller must
 * rebuild the directory listing afterwards. */
bool textView(const char *path);
