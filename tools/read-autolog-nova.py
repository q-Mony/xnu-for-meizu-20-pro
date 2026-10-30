#!/usr/bin/env python3
"""Read only the MEIZULOG window through Android+su; never reboot or switch slots.
Ported protocol from workspace/tools/read-xnu-log.py (f4abf01bde4c2f4b8900cf361291589df7a4e9d754ee1058b71512c61fbd4c52).
"""
import argparse,datetime,hashlib,json,shlex,struct,subprocess
from pathlib import Path
NOVA=Path(__file__).resolve().parents[1]
PARTITION='/dev/block/by-name/unixnu'
WINDOW=32*1024*1024
BLOCK=4096
EXPECTED_DEVICE_TAG='8258937a53b0'
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',required=True,type=Path);args=ap.parse_args()
 out=args.output.resolve();assert out.is_relative_to(NOVA/'logs-nova') and not out.exists(),'Need fresh Nova log directory'
 p=subprocess.run(['adb','devices'],capture_output=True,text=True,check=True,timeout=8)
 ids=[s.split()[0] for s in p.stdout.splitlines()[1:] if s.strip() and s.split()[1]=='device']
 assert len(ids)==1 and hashlib.sha256(ids[0].encode()).hexdigest()[:12]==EXPECTED_DEVICE_TAG,'Unexpected device'
 base=['adb','-s',ids[0]]
 def root(command):
  return subprocess.run(base+['shell','su -c '+shlex.quote(command)],capture_output=True,text=True,check=True,timeout=15).stdout.strip()
 assert root('id').startswith('uid=0('),'Root required'
 size=int(root('blockdev --getsize64 '+PARTITION));assert size>WINDOW and size%BLOCK==0
 out.mkdir()
 raw=out/'autolog-window-nova.bin'
 command='dd if='+PARTITION+' bs=4096 skip='+str((size-WINDOW)//BLOCK)+' count=8192 2>/dev/null'
 with raw.open('xb') as f:
  result=subprocess.run(base+['exec-out','su -c '+shlex.quote(command)],stdout=f,stderr=subprocess.PIPE,check=True,timeout=45)
 data=raw.read_bytes();assert len(data)==WINDOW,'Short/contaminated window read'
 assert data[:8]==b'MEIZULOG','No valid log header'
 total,count,dropped,reserved=struct.unpack_from('<QQII',data,8)
 assert count<=WINDOW-BLOCK,'Invalid count'
 text=data[BLOCK:BLOCK+count]
 (out/'autolog-nova.txt').write_bytes(text)
 report={'captured_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'partition':PARTITION,'partition_bytes':size,'window_bytes':len(data),'ring_total':total,'text_bytes':count,'dropped':dropped,'reserved':reserved,'raw_sha256':hashlib.sha256(data).hexdigest(),'text_sha256':hashlib.sha256(text).hexdigest(),'nul_bytes':text.count(bytes([0])),'no_reboot_or_slot_change':True,'raw_partition_read_only':True}
 (out/'capture-nova.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if __name__=='__main__':main()
