#!/usr/bin/env python3
"""Verify RISC-V firmware regions, SDK image metadata and SD packages."""
import hashlib
import json
import struct
from pathlib import Path
from uf2 import FAMILIES,inspect_uf2
from package import inspect,inspect_image
ROOT=Path(__file__).resolve().parents[1]

def reject(fn,data):
    try: fn(data)
    except ValueError: return
    raise AssertionError('Invalid image was accepted')

def main():
    builds=packages=0
    configurations=[]
    for board in ('rp2350-riscv','rp2350-pizero-riscv'):
        for profile,count in (('standalone',33),('menu',25)):
            if (ROOT/'dist'/board/profile).exists():configurations.append((board,profile,count))
    assert configurations, 'No firmware manifests found'
    for board,profile,count in configurations:
        directory=ROOT/'dist'/board/profile
        entries=json.loads((directory/'builds.json').read_text())
        assert len(entries)==count
        if board=='rp2350-pizero-riscv':
            assert all(e['board']=='pizero' for e in entries)
            assert all(e['fqbn']=='rp2040:rp2040:rpgame2350_pizero:arch=riscv,freq=150' for e in entries)
        for entry in entries:
            assert entry['profile']==profile and entry['target']=='rp2350-riscv'
            uf2=(directory/(entry['name']+'.uf2')).read_bytes()
            assert hashlib.sha256(uf2).hexdigest()==entry['sha256']
            inspect_uf2(uf2,'rp2350-riscv')
            origin=0x10080000 if profile=='menu' else 0x10000000
            limit=0x10080000 if entry['name']=='SDLauncher' else 0x103f0000
            for off in range(0,len(uf2),512):
                addr,size=struct.unpack_from('<2I',uf2,off+12)
                family=struct.unpack_from('<I',uf2,off+28)[0]
                if family==FAMILIES['rp2350-riscv']:assert origin<=addr<addr+size<=limit
            binary=(directory/(entry['name']+'.bin')).read_bytes()
            assert len(binary)==entry['image_bytes']
            inspect_image(binary,origin)
            if profile=='menu':
                rpg=(directory/(entry['name']+'.rpg')).read_bytes();inspect(rpg)
                assert rpg[512:512+len(binary)]==binary
                packages+=1
            builds+=1
        sample=(directory/(entries[0]['name']+'.uf2')).read_bytes()
        for wrong in ('rp2040','rp2350-arm'):reject(lambda d:inspect_uf2(d,wrong),sample)
        for bad in (sample[:-1],sample[:-512],sample+sample[-512:],b'\0'+sample[1:]):
            reject(lambda d:inspect_uf2(d,'rp2350-riscv'),bad)
    menu_dir=next(ROOT/'dist'/board/profile for board,profile,_ in configurations if profile=='menu')
    sample=next(menu_dir.glob('*.rpg')).read_bytes()
    for index in (0,8,12,16,20,24,32,508,512,len(sample)-1):
        bad=bytearray(sample);bad[index]^=1;reject(inspect,bad)
    reject(inspect,sample[:-1])
    card=ROOT/'sdcard'
    catalog=json.loads((card/'catalog.json').read_text())
    assert len(catalog)==25 and len({e['file'] for e in catalog})==25
    for entry in catalog:
        board={'pizero':'rp2350-pizero-riscv','pico2':'rp2350-riscv'}[entry['board']]
        packaged=(ROOT/'dist'/board/'menu'/(entry['sketch']+'.rpg')).read_bytes()
        staged=(card/entry['file']).read_bytes()
        assert staged==packaged, f"Stale or wrong-board card package: {entry['file']}"
        assert inspect(staged)=={key:value for key,value in entry.items()
                                 if key not in ('sketch','board','file')}
    assert {p.relative_to(card).as_posix() for p in (card/'GAMES').glob('*.RPG')}=={e['file'] for e in catalog}
    checked=set()
    for line in (card/'SHA256SUMS').read_text().splitlines():
        digest,name=line.split('  ',1)
        assert hashlib.sha256((card/name).read_bytes()).hexdigest()==digest, name
        checked.add(name)
    assert checked=={p.relative_to(card).as_posix() for p in card.rglob('*')
                     if p.is_file() and p.name!='SHA256SUMS'}
    print(f'PASS: {builds} UF2s, region bounds and ROM IMAGE_DEF loops; {packages} CRC-checked RPG packages.')
    print('PASS: 25 staged card packages match their board builds; card catalog and every asset checksum verified.')
    print('PASS: wrong-family, truncated, duplicate and damaged files rejected.')

if __name__=='__main__':main()
