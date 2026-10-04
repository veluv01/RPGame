#!/usr/bin/env python3
"""Stage the complete SD card tree from verified menu-linked packages."""
import hashlib
import json
import argparse
from pathlib import Path
import shutil
from package import inspect
ROOT=Path(__file__).resolve().parents[1]
NAMES={
 'RPBackgammon':'BACKGAM','RPBingo':'BINGO','RPBlackjack':'BLACKJAK','RPBoardwalk':'BOARDWLK',
 'RPCheckers':'CHECKERS','RPChess':'CHESS','RPCraps':'CRAPS','RPCrossword':'CROSSWRD',
 'RPDominoes':'DOMINOES','RPFour':'FOUR','RPMahjong':'MAHJONG','RPPoker':'POKER',
 'RPRoulette':'ROULETTE','RPSlots':'SLOTS','RPSnakes':'SNAKES','RPSolitaire':'SOLITARE',
 'RPTicTacToe':'TICTACTO','RPWordWheel':'WORDWHEL','RPWords':'WORDS','RPYacht':'YACHT',
 'HardwareCheck':'HWTEST','RPMultiSprite':'SPRITES','RPStlView':'STLVIEW',
 'RPFileBrowser':'BROWSER','RPSDtoUSB':'SDUSB'}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board',choices=('pizero','pico2'),default='pizero')
    parser.add_argument('--output',type=Path,default=ROOT/'sdcard')
    args=parser.parse_args()
    board_dir='rp2350-pizero-riscv' if args.board=='pizero' else 'rp2350-riscv'
    dest=args.output;(dest/'GAMES').mkdir(parents=True,exist_ok=True)
    entries=[]
    for name,short in NAMES.items():
        src=ROOT/'dist'/board_dir/'menu'/f'{name}.rpg';data=src.read_bytes();h=inspect(data)
        out=dest/'GAMES'/f'{short}.RPG';out.write_bytes(data)
        entries.append(dict(sketch=name,board=args.board,file=out.relative_to(dest).as_posix(),**h))
    for game in ('RPWords','RPWordWheel','RPCrossword'):
        shutil.copytree(ROOT/'games'/game/'sdcard',dest,dirs_exist_ok=True)
    shutil.copy2(ROOT/'apps/RPMultiSprite/sample/WALK.BIN',dest/'WALK.BIN')
    (dest/'MODELS').mkdir(exist_ok=True)
    for src in (ROOT/'apps/RPStlView/sample').glob('*.STL'):shutil.copy2(src,dest/'MODELS'/src.name)
    (dest/'catalog.json').write_text(json.dumps(entries,indent=2)+'\n')
    checks=''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(dest).as_posix()}\n'
                   for p in sorted(dest.rglob('*')) if p.is_file() and p.name!='SHA256SUMS')
    (dest/'SHA256SUMS').write_text(checks)
    print(f'Staged {len(entries)} {args.board} packages, dictionaries, phrases, crosswords, sprites and six STL models.')

if __name__=='__main__':main()
