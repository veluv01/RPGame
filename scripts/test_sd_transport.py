#!/usr/bin/env python3
"""Test the actual RP SD transport with shared and PiZero dedicated SPI."""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rpgame-transport-') as temporary:
    out=Path(temporary)
    base=['g++','-std=c++17','-O2','-fsanitize=address,undefined','-DARDUINO_ARCH_RP2040','-DPICO_RP2350=1','-DF_CPU=150000000',
          '-I'+str(ROOT/'tests/sd_rp_host'),'-I'+str(ROOT/'libraries/RPGfx/src'),'-I'+str(ROOT/'libraries/RPGameSD/src/utility')]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    for name,flags in [('shared',['-DPICO_RP2350A=1']),('pizero',['-DPICO_RP2350A=0','-DARDUINO_RPGAME_RP2350_PIZERO'])]:
        exe=out/name
        subprocess.run(base+flags+[str(ROOT/'tests/test_sd_transport.cpp'),str(ROOT/'libraries/RPGameSD/src/utility/Sd2Card.cpp'),'-o',str(exe)],check=True,env=env)
        subprocess.run([str(exe)],check=True,env=env)
    pins=out/'pins.cpp';pins.write_text('#include <RPGamePins.h>\n')
    # Reject physical pin collisions, high GPIOs on the A package, and a
    # shared peripheral configured on two incompatible pad sets.
    for flags in [
        ['-DPICO_RP2350A=1','-DARDUINO_RPGAME_RP2350_PIZERO'],
        ['-DPICO_RP2350A=0','-DARDUINO_RPGAME_RP2350_PIZERO','-DRPGAME_BTN_UP=30'],
        ['-DPICO_RP2350A=0','-DRPGAME_SD_SPI_SCK=18','-DRPGAME_SD_SPI_MOSI=19','-DRPGAME_SD_SPI_MISO=16'],
        ['-DPICO_RP2350A=0','-DARDUINO_RPGAME_RP2350_PIZERO','-DRPGAME_SD_SPI_MISO=41']]:
        result=subprocess.run(base+flags+['-c',str(pins),'-o',str(out/'pins.o')],capture_output=True,text=True)
        assert result.returncode and 'static assertion failed' in result.stderr,result.stderr
    print('Pin guards: A/B package, control collisions, shared-pin mismatch and invalid SD RX: PASS')
