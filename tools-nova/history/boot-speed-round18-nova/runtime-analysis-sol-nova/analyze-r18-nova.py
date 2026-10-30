#!/usr/bin/env python3
"""R18 trace comparison by Sol directly. New file; no previous version. No device/dsh calls."""
from pathlib import Path
import collections,datetime,hashlib,json,re
N=Path(__file__).resolve().parents[3];OUT=Path(__file__).resolve().parent
LOG=N/'logs-nova/boot-speed-round18-nova/candidate-run-nova'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
cap=json.loads((LOG/'capture-nova.json').read_text());txt=LOG/'autolog-nova.txt';s=txt.read_text()
assert sha(txt)==cap['text_sha256']=='ebb550998cc7f4cad5c2fda939cdef3c82e557181328804afc36c161561e71dd'
assert sha(LOG/'autolog-window-nova.bin')==cap['raw_sha256']=='b032c84b882184ee3e9991c0f69b4293d4566c72e88aa89d56d1db17c0d70804'
assert cap['dropped']==0 and cap['nul_bytes']==0 and cap['window_bytes']==33554432
pat=re.compile(r'\[nova-ts:v1 c=cntvct t=([0-9a-f]+) f=([0-9a-f]+) k=([A-Z])\]');ms=list(pat.finditer(s));rec=[]
for i,m in enumerate(ms):
 hz=int(m[2],16);assert hz==19200000
 rec.append({'line':s.count('\n',0,m.start())+1,'ticks':int(m[1],16),'seconds':int(m[1],16)/hz,'message':s[m.end():ms[i+1].start() if i+1<len(ms) else len(s)].strip()})
def rows(k):return [r for r in rec if k in r['message']]
def fields(r):return dict(re.findall(r'(\w+)=([^\s]+)',r['message']))
def one(k):
 a=rows(k);assert len(a)==1,(k,len(a));return a[0]
assert len(rows('Darwin Kernel Version'))==1
baseline_path=N/'artifacts-nova/boot-speed-round17-nova/runtime-analysis-sol-nova/evidence-nova.json';base=json.loads(baseline_path.read_text())
phase={}
for r in rows('nova: bootphase '):
 f=fields(r);assert int(f['result'])==0
 phase.setdefault(f['phase'],{})[f['edge']]={'ticks':int(f['ticks']),'line':r['line']}
for f in phase.values():assert set(f)=={'begin','end'};assert f['end']['ticks']>=f['begin']['ticks'];f['elapsed_seconds']=(f['end']['ticks']-f['begin']['ticks'])/19200000
warm_records=rows('nova: warmload status=');assert warm_records
warm_fields=fields(warm_records[0]);assert warm_fields['status']=='0' and warm_fields['capture_id']=='4013558640' and warm_fields['bytes']=='231153664'
assert all(fields(r)==warm_fields for r in warm_records),'warm stats changed between snapshots'
warm={k:int(warm_fields[k])/19200000 for k in ['start_ticks','source_ticks','read_ticks','hash_ticks','import_ticks','publish_ticks','end_ticks']};warm['wall_start_to_end_seconds']=warm['end_ticks']-warm['start_ticks'];warm['hash_nested_in_import']=True
anchors={}
for name in ['launchd','SpringBoard','backboardd']:
 a=[r for r in rows('meizu: exec-map ') if fields(r).get('name')==name];assert len(a)==1
 r=a[0];f=fields(r);anchors[name]={'line':r['line'],'capture_ticks':int(f['capture_ticks']),'seconds':int(f['capture_ticks'])/19200000,'pid':int(f['pid']),'taskgen':int(f['taskgen'])}
r=one('meizu: setup-arm ');f=fields(r);setup_ticks=int(f['capture_ticks']);anchors['Setup_armed']={'line':r['line'],'seconds':setup_ticks/19200000,'capture_ticks':setup_ticks,'pid':int(f['pid']),'taskgen':int(f['taskgen']),'is_hello_first_frame':False}
life=one('nova: life v=16');lf=fields(life);assert lf['flags']=='req:1,recv:1,copyout:1,reply:1,reply_end:1,finish:1'
assert lf['recv_server_pid']==lf['reply_server_pid']==str(anchors['backboardd']['pid']);assert lf['recv_server_taskgen']==lf['reply_server_taskgen']==str(anchors['backboardd']['taskgen']);assert lf['recv_server_tid']==lf['reply_server_tid']
assert lf['pid']==str(anchors['SpringBoard']['pid']) and lf['taskgen']==str(anchors['SpringBoard']['taskgen'])
assert lf['request_id']==lf['recv_id']=='6001009' and lf['reply_id']=='6001109'
assert lf['recv_result']==lf['reply_result']==lf['finish_result']=='0'
keys=['req_ticks','recv_ticks','copyout_ticks','reply_begin_ticks','reply_end_ticks','finish_ticks'];ticks=[int(lf[k]) for k in keys];assert ticks==sorted(ticks)
segments={keys[i]+'__'+keys[i+1]:(ticks[i+1]-ticks[i])/19200000 for i in range(len(ticks)-1)};segments['receive_to_sender_finish']=(ticks[-1]-ticks[1])/19200000;segments['request_to_sender_finish']=(ticks[-1]-ticks[0])/19200000
cs=rows('nova: cs4k ');totals={k:sum(int(fields(r)[k]) for r in cs) for k in ['bytes','reads','total_ms','read_ms','hash_ms']}
exits=rows('meizu: xr ');core=[r for r in exits if re.search(r'comm=(SpringBoard|backboardd|Setup)( |$)',r['message'])];faults={k:rows(k) for k in ['Assertion failed','sbfault','panic(cpu','panic=']}
swaps=rows('NEGRONI FB: swap ');last_swap=swaps[-1] if swaps else None
freeze_path=N/'artifacts-nova/boot-speed-round18-nova/build-package-nova/source-freeze-r18-nova.json';freeze=json.loads(freeze_path.read_text())['source_sha256'];sources=['unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp','unixnu-main-nova/drivers/NativeRamBlock/WarmLoad.h','unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c','unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/libkern/crypto/native_sha256.h','unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/osfmk/ipc/mach_msg.c','unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/bsd/kern/kern_exec.c','unixnu-main-nova/drivers/NegroniDisplay/NegroniFramebuffer.cpp'];bound={p:sha(N/p) for p in sources}
for p,h in bound.items():assert h==freeze[p],p
post=json.loads((LOG.parent/'post-boot-device-nova.json').read_text());assert post['status']=='ANDROID_B_RECOVERY_AND_READONLY_PRECHECK_PASS' and post['current_slot']=='b';assert post['partition_hashes']['boot_a']=='be40edfedadbc161e3e86864c9d8803243c34426f78007f983213c07c879c21b';assert post['partition_hashes']['boot_b']=='7df205f172e553f76a68726c67ed7fcad112784f320941ce212433dee21745a2'
comparisons={}
def compare(label,old,new):comparisons[label]={'r17_seconds':old,'r18_seconds':new,'saved_seconds':old-new,'reduction_percent':(old-new)/old*100 if old else None}
for k in ['hash_ticks','read_ticks','import_ticks','wall_start_to_end_seconds']:compare('warm_'+k,base['warmload_seconds'][k],warm[k])
for k,v in phase.items():compare('phase_'+k,base['phase_intervals'][k]['elapsed_seconds'],v['elapsed_seconds'])
for k,v in anchors.items():compare('clock_'+k,base['anchors'][k]['seconds'],v['seconds'])
compare('target_request_to_receiver',base['lifecycle']['segments_seconds']['req_ticks__recv_ticks'],segments['req_ticks__recv_ticks'])
result={'new_file_no_previous_version':True,'status':'R18_WARM_SHA_STAGE_IMPROVEMENT_MEASURED_OTHER_BOTTLENECK_REMAINS','reader':'Sol direct; no dsh or subagents','generated_at':datetime.datetime.now().astimezone().isoformat(),'capture':cap,'record_count':len(rec),'first_timestamp_seconds':rec[0]['seconds'],'last_timestamp_seconds':rec[-1]['seconds'],'clock_epoch':'CNTVCT aligned with Mach; Boot-menu origin not recorded','warmload_fields':warm_fields,'warmload_seconds':warm,'phase_intervals':phase,'anchors':anchors,'lifecycle':{'line':life['line'],'fields':lf,'segments_seconds':segments},'cs4k_count':len(cs),'cs4k_totals':totals,'comparisons':comparisons,'faults':faults,'tracked_core_process_exits':core,'all_exit_counts':dict(collections.Counter(fields(r).get('comm','?') for r in exits)),'swap_logged_count':len(swaps),'last_logged_swap':last_swap,'hello_confirmed_by_user':True,'actual_hello_first_frame_seconds':None,'boot_menu_to_hello_seconds':None,'black_screen_this_run':'user has not reported whether it recurred; existing sampling cannot prove absence','source_hashes_current_and_frozen':bound,'baseline_evidence_sha256':sha(baseline_path),'post_capture_device_state':post,'device_left':'Android B','production_edits':False,'new_candidate_built_or_installed_this_turn':False,'limitations':['One paired phone run; stage difference is measured but no multi-run variance bound.','Overlapping stages cannot be added to derive total savings.','Setup armed is not Hello first frame and clock has no Boot-menu origin.','16-swap cap and no power/frame-content trace cannot establish black-screen cause or absence.']}
with (OUT/'evidence-nova.json').open('x') as f:json.dump(result,f,ensure_ascii=False,indent=2);f.write('\n')
print(json.dumps({k:result[k] for k in ['record_count','last_timestamp_seconds','comparisons','cs4k_count','cs4k_totals','all_exit_counts','swap_logged_count']},ensure_ascii=False,indent=2))
