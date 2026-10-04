"""Preview of the display font (tools/art/common/font.txt), drawn as the game
draws its lettering: gradient fill, ink outline, a shadow a pixel down and
right (the repository's tools/fonts/font_preview.py, with this game's lines).

    python tools/font_preview.py [OUT.png]      (default build/assets/font_sample.png)
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str((next(p for p in Path(__file__).resolve().parents if (p / "libraries" / "RPGame").is_dir()) / "tools")))     # the repository's tools/: fonts.font_preview
from assets import load_font, rgb  # noqa: E402
from fonts import font_preview  # noqa: E402

# The logo's 'o' is a checker, not a glyph: missing characters leave its room.
SAMPLES = ["Backgammon", "BACKGAMMON!", "DOUBLES! HIT!", "GAMMON!", "PRIME!", "CLOSED OUT!", "YOU WIN!",
           "MATCH! DANCE!", "ABCDEFGHI", "JKLMNOPQR", "STUVWXYZ", "0123456789", "?+-.,:' TAKE PASS"]


if __name__ == "__main__":
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / "build/assets/font_sample.png"
    font_preview.render(SAMPLES, load_font(), rgb, out)
