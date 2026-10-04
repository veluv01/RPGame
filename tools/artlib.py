"""Where a game's art comes from: its own tools/art/ first, then the shared
tools/art/common/ (the files that were byte-identical in several games:
the dealer and his faces, the pointing glove, the display font, the card
art, the chips, the end-screen lettering).

    import artlib
    path = artlib.art(HERE, "dealer.png")      # HERE: the game's tools/ folder

`art()` returns the game's own file when it has one, else the common one,
else the game's (non-existent) path, so `.exists()` checks keep working.
A game that wants its own version of a shared file just puts it in its
tools/art/.
"""
from pathlib import Path

COMMON = Path(__file__).resolve().parent / "art" / "common"


def art(tools_dir, name):
    own = Path(tools_dir) / "art" / name
    if own.exists():
        return own
    shared = COMMON / name
    return shared if shared.exists() else own


def common_dir():
    return COMMON
