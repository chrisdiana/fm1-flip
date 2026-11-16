#!/usr/bin/env python3
"""Merge already-built board binaries for first install; never upload to hardware."""
import argparse, hashlib, json, pathlib, subprocess, sys
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('boards',nargs='*',default=['cardputer-adv','sticks3','cardputer'],choices=['cardputer-adv','sticks3','cardputer'])
p.add_argument('--esptool',type=pathlib.Path,default=pathlib.Path.home()/'.platformio/packages/tool-esptoolpy/esptool.py')
a=p.parse_args()
out=root/'build/releases';out.mkdir(parents=True,exist_ok=True)
manifest={}
inputs = sorted([root/'platformio.ini', root/'tools/embed_assets.py', root/'web/index.html',
                 *(root/'src').rglob('*'), *(root/'include').rglob('*'), *(root/'partitions').glob('*.csv')])
inputs = [path for path in inputs if path.is_file()]
source_hash=hashlib.sha256()
for path in inputs:
    source_hash.update(path.relative_to(root).as_posix().encode()+b'\0'+path.read_bytes()+b'\0')
for board in a.boards:
    build=root/'.pio/build'/board
    for name in ['bootloader.bin','partitions.bin','firmware.bin']:
        if not (build/name).is_file(): raise SystemExit('Build first: pio run -e '+board)
    if any(path.stat().st_mtime_ns > (build/'firmware.bin').stat().st_mtime_ns for path in inputs):
        raise SystemExit('Source is newer than '+board+' binary; rebuild before packaging')
    target=out/('fm1-flip-'+board+'-factory.bin')
    subprocess.run([sys.executable,str(a.esptool),'--chip','esp32s3','merge-bin','--flash-size','8MB','-o',str(target),'0x0',str(build/'bootloader.bin'),'0x8000',str(build/'partitions.bin'),'0x10000',str(build/'firmware.bin')],check=True)
    manifest[target.name]={'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'bytes':target.stat().st_size,
                          'sourceSha256':source_hash.hexdigest(),'hardwareValidated':False}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out)
