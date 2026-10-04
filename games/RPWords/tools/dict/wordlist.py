"""Word sources for the CHWords dictionary tools.

ENABLE (public domain) is the full list.  The flash core list is every
2- and 3-letter ENABLE word plus the most frequent 4-8 letter ones, ranked
by a Google Books derived count list (build-time use only).
"""
import hashlib
import urllib.request
from pathlib import Path

DATA = Path(__file__).resolve().parent / "data"

ENABLE_URL = "https://raw.githubusercontent.com/dolph/dictionary/master/enable1.txt"
ENABLE_SHA = "3f16130220645692ed49c7134e24a18504c2ca55b3c012f7290e3e77c63b1a89"
FREQ_URL = "https://norvig.com/google-books-common-words.txt"

CORE_MAX_LEN = 8
BOARD = 15


def _fetch(url, path):
    if not path.exists():
        DATA.mkdir(parents=True, exist_ok=True)
        print(f"downloading {url}")
        urllib.request.urlretrieve(url, path)
    return path


def load_enable():
    """Sorted list of every ENABLE word that fits on the board."""
    path = _fetch(ENABLE_URL, DATA / "enable1.txt")
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != ENABLE_SHA:
        raise SystemExit("enable1.txt does not match the pinned SHA-256")
    words = [w for w in raw.decode("ascii").split() if 2 <= len(w) <= BOARD]
    assert all(w.isalpha() and w.islower() for w in words)
    assert words == sorted(set(words))
    return words


def load_freq():
    """word -> count from the Google Books common-words list."""
    path = _fetch(FREQ_URL, DATA / "gbooks_common.txt")
    freq = {}
    for line in path.read_text("ascii").splitlines():
        word, count = line.split()
        freq[word.lower()] = int(count)
    return freq


def load_blocked():
    """Words kept out of the core list (the CPU never plays them)."""
    path = Path(__file__).resolve().parent / "blocked.txt"
    if not path.exists():
        return set()
    return {w.strip() for w in path.read_text("ascii").split() if w.strip()}


def ranking(enable):
    """4-8 letter ENABLE words, most frequent first (ties alphabetical)."""
    freq = load_freq()
    blocked = load_blocked()
    pool = [w for w in enable if 4 <= len(w) <= CORE_MAX_LEN and w not in blocked]
    return sorted(pool, key=lambda w: (-freq.get(w, 0), w))


def ranked_core(enable, n, rank=None):
    """Sorted core list of n words: all 2-3 letter words, then by rank."""
    blocked = load_blocked()
    short = [w for w in enable if len(w) <= 3 and w not in blocked]
    rank = rank if rank is not None else ranking(enable)
    return sorted(short + rank[: max(0, n - len(short))])
