#!/usr/bin/env python3
"""Test a staged package on contiguous and fragmented FAT card images."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'libraries/RPGameSD/tools'))
import fatimg
with tempfile.TemporaryDirectory(prefix='rpgame-cardtest-') as tmp:
    tmp=Path(tmp);exe=tmp/'test'
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    cmd=['g++','-std=c++17','-O2','-fsanitize=address,undefined','-DCHSIM',
         '-I'+str(ROOT/'libraries/RPGameSD/src'),'-I'+str(ROOT/'libraries/RPGame/src'),
         str(ROOT/'tests/test_package_fat.cpp'),str(ROOT/'libraries/RPGameSD/src/Fat.cpp'),
         str(ROOT/'libraries/RPGame/src/rpgame/Flash.cpp'),str(ROOT/'libraries/RPGame/src/rpgame/Package.cpp'),'-o',str(exe)]
    subprocess.run(cmd,check=True,env=env)
    data=(ROOT/'sdcard/GAMES/FOUR.RPG').read_bytes()
    for fragmented in (False,True):
        img=tmp/'card.img'
        fatimg.build_image(str(img),{'GAMES/FOUR.RPG':data},fs='fat16',fragment=7 if fragmented else 1)
        subprocess.run([str(exe),str(img)],check=True,env=env)
