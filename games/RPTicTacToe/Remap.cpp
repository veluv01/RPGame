// The sprite colour remaps (Remap.h): entry i is the colour drawn for
// the art's colour i.
#include "Remap.h"
#include <RPGame.h>

const uint8_t RM_CPU[16] = {INK, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE,
                            RED, WINE, BLUE, NAVY, SKIN, CYAN, FX_A, FX_B};
const uint8_t RM_HOT[16] = {FX_A, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
const uint8_t RM_BLUE[16] = {INK, WHITE, FELT_DK, FELT, FELT_LT, SILVER, BLUE, NAVY,
                             GOLD, WOOD, BLUE, NAVY, CYAN, CYAN, FX_A, FX_B};
const uint8_t RM_BLUEHOT[16] = {FX_A, WHITE, FELT_DK, FELT, FELT_LT, SILVER, BLUE, NAVY,
                                GOLD, WOOD, BLUE, NAVY, CYAN, CYAN, FX_A, FX_B};
const uint8_t RM_SHADE[16] = {INK, SILVER, FELT_DK, FELT_DK, FELT, NAVY, WINE, INK,
                              WOOD, WINE, NAVY, INK, RED, BLUE, FX_A, FX_B};
const uint8_t RM_BLUESHADE[16] = {INK, SILVER, FELT_DK, FELT_DK, FELT, NAVY, NAVY, INK,
                                  WOOD, WINE, NAVY, INK, BLUE, BLUE, FX_A, FX_B};
const uint8_t RM_ALERT[16] = {INK, RED, FELT_DK, FELT, FELT_LT, WINE, RED, WINE,
                              RED, WINE, BLUE, NAVY, SKIN, CYAN, FX_A, FX_B};
