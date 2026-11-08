"""Encrypted loopback capacity ramp. Never connects to Home Assistant or the LAN."""
from __future__ import annotations
import asyncio,collections,json,os,random,resource,statistics,time
from pathlib import Path
from harness import Device,MemoryCounterStore,make_hub,wait_for,PORT
ROOT=Path(os.environ.get('ETCORE_BOUNDED_OUTPUT',str(Path(__file__).resolve().parent/'results'/'bounded')))
ROOT.mkdir(parents=True,exist_ok=True)
COUNTS=[int(x) for x in os.environ.get("ETCORE_CEILING_COUNTS","2000,4000,8000,16000").split(",")]
LEASE_MS=2000
TTL=10
STEADY_S=60
assert PORT==18560
_base_make_hub=make_hub
class PacketCounter:
 def __init__(self):self.count=0
 def append(self,_packet):self.count+=1
 def __len__(self):return self.count

def make_hub(*args,**kwargs):
 h=_base_make_hub(*args,**kwargs)
 h.rec.rx=PacketCounter();h.rec.tx=PacketCounter()
 h.hass.bus.events=collections.deque(maxlen=128)
 return h

def socket_drops(hub):
 sock=hub._transport.get_extra_info('socket');inode=str(os.fstat(sock.fileno()).st_ino)
 for line in Path('/proc/net/udp').read_text().splitlines()[1:]:
  fields=line.split()
  if len(fields)>10 and fields[9]==inode:return int(fields[-1])
 return None

async def pubseqs(devs):
 out={}
 for i,d in enumerate(devs):
  out[d.id]=pubseq(d)
  if i%50==0:await asyncio.sleep(0)
 return out


def rss(pid=None):
 try:
  lines=Path(f'/proc/{pid or "self"}/status').read_text().splitlines()
  return int(next(s for s in lines if s.startswith('VmRSS:')).split()[1])
 except (OSError,StopIteration):return 0

def pubseq(d):
 return {int(x.split()[1]) for x in d.poll_lines() if x.startswith('PUB ')}

def percentile(values,p):
 if not values:return None
 return sorted(values)[min(len(values)-1,max(0,int(len(values)*p+.999)-1))]

def resource_check():
 mem={line.split(':')[0]:line.split(':')[1].strip() for line in Path('/proc/meminfo').read_text().splitlines()}
 available=int(mem['MemAvailable'].split()[0])
 if available < 4*1024*1024:raise RuntimeError('HOST_RESOURCE_GUARD: less than 4 GiB available; not a protocol limit')
 # Keep substantial PID room for the desktop and other applications.
 relative=next((line[3:] for line in Path('/proc/self/cgroup').read_text().splitlines() if line.startswith('0::')), '/')
 path=Path('/sys/fs/cgroup')/relative.lstrip('/')
 if (path/'pids.max').exists():
  limit=(path/'pids.max').read_text().strip();used=int((path/'pids.current').read_text())
  if limit!='max' and int(limit)-used<2048:raise RuntimeError('HOST_RESOURCE_GUARD: PID reserve reached; not a protocol limit')
 if os.getloadavg()[0] > .85*(os.cpu_count() or 1):raise RuntimeError('HOST_RESOURCE_GUARD: host load reserve reached; not a protocol limit')
 return available

async def run(n):
 randomizer=random.Random(1000+n)
 psk=os.urandom(32).hex() # Ephemeral test key; not a production credential.
 devs=[];hub=None
 result={'devices':n,'lease_ms':LEASE_MS,'ttl_s':TTL,'steady_s':STEADY_S,'state_interval_ms':10000}
 try:
  hub=make_hub(asyncio.get_running_loop(),psk,MemoryCounterStore())
  received=collections.defaultdict(set);off=[]
  def capture(msg):
   if msg.get('type')=='state' and isinstance((msg.get('payload') or {}).get('sample_seq'),int):received[msg['id']].add(msg['payload']['sample_seq'])
  hub.register_listener(capture);hub.register_status_listener(lambda st:off.append((time.monotonic(),dict(st))))
  await hub.async_start();t=time.monotonic()
  for i in range(n):
   if i%25==0:resource_check()
   devs.append(Device(f'load{n:03d}-{i:03d}','switch.relay',f'127.0.{1+i//250}.{1+i%250}',psk,LEASE_MS,TTL,env={'ETCORE_SIM_STATE_MS':'10000','ETCORE_SIM_SETPORT':'18560'}))
  online=lambda:sum(bool(info.get('online')) for info in hub.devices.values())
  result['all_online']=await wait_for(lambda:online()==n,60,.05);result['join_s']=time.monotonic()-t
  if not result['all_online']:raise RuntimeError(f'Only {online()}/{n} online')
  device_rss=sum(rss(d.proc.pid) for d in devs) # Outside the timed/report-loss window.
  await asyncio.sleep(3)
  baseline=await pubseqs(devs);off.clear()
  drops0=socket_drops(hub)
  recv_buffer=hub._transport.get_extra_info('socket').getsockopt(__import__('socket').SOL_SOCKET,__import__('socket').SO_RCVBUF)
  rx0=len(hub.rec.rx);cpu0=time.process_time();t=time.monotonic();rss0=rss()
  lag=[];sample_memory=[];ended=False
  async def monitor():
   while not ended:
    target=time.monotonic()+.1;await asyncio.sleep(.1);lag.append(max(0,time.monotonic()-target));sample_memory.append(rss())
  watch=asyncio.create_task(monitor())
  await asyncio.sleep(STEADY_S)
  elapsed=time.monotonic()-t;cpu=time.process_time()-cpu0
  result.update(packets_per_s=(len(hub.rec.rx)-rx0)/elapsed,hub_cpu_one_core_pct=100*cpu/elapsed,
                false_offlines=sum(not st['online'] for _,st in off),hub_rss_kb=rss(),hub_rss_growth_kb=rss()-rss0,
                simulated_devices_rss_kb=device_rss,event_loop_lag_p95_ms=1000*(percentile(lag,.95) or 0),event_loop_lag_max_ms=1000*max(lag,default=0))
  final_published=await pubseqs(devs)
  await asyncio.sleep(.25) # Drain reports already counted before comparing.
  drops1=socket_drops(hub)
  ended=True;await watch
  generated=missing=0
  for d in devs:
   sent=final_published[d.id]-baseline[d.id];generated+=len(sent);missing+=len(sent-received[d.id])
  result.update(state_reports_generated=generated,state_reports_missing=missing,hub_receive_buffer_bytes=recv_buffer,kernel_socket_drops=None if drops0 is None or drops1 is None else drops1-drops0,collector='bounded counters; child RSS outside measurement window')
  resource_check()
  if missing or result['false_offlines']:
   raise RuntimeError('STEADY_LOAD_FAILURE: reports missing or false offline events')
  lat=[];failures=0;targets=randomizer.sample(devs,min(50,n))
  for d in targets:
   previous=len(d.commands());t=time.monotonic();hub.send_command(d.id,'switch.relay',{'on':True})
   ok=await wait_for(lambda:not hub._pending,2,.002)
   if ok and await wait_for(lambda:len(d.commands())>previous,1,.005):lat.append((time.monotonic()-t)*1000)
   else:failures+=1
  result.update(commands=len(targets),commands_failed=failures,command_ack_median_ms=statistics.median(lat) if lat else None,command_ack_p95_ms=percentile(lat,.95),command_ack_max_ms=max(lat,default=None))
  result['initial_hub_decrypt_failures']=hub._decrypt_fail_count
  result['initial_hub_replays_dropped']=hub._replay_count
  store=hub._counter_store;await hub.async_stop();hub=make_hub(asyncio.get_running_loop(),psk,store);states=set();off=[]
  hub.register_listener(lambda msg:states.add(msg['id']) if msg.get('type')=='state' else None)
  hub.register_status_listener(lambda st:off.append((time.monotonic(),dict(st))))
  t=time.monotonic();await hub.async_start()
  named=lambda:sum(bool(info.get('online') and info.get('name')) for info in hub.devices.values())
  result['restart_recovered_all']=await wait_for(lambda:named()==n and len(states)==n,60,.05);result['restart_recovery_s']=time.monotonic()-t
  await asyncio.sleep(1);off.clear();victims=randomizer.sample(devs,min(10,n));ids={d.id for d in victims};t=time.monotonic()
  for d in victims:d.kill()
  detected=lambda:{st['id'] for _,st in off if st['id'] in ids and not st['online']}
  await wait_for(lambda:len(detected())==len(ids),TTL+10,.05)
  times=[at-t for at,st in off if st['id'] in ids and not st['online']]
  result.update(unplugged=len(ids),offline_detected=len(detected()),offline_detection_min_s=min(times,default=None),offline_detection_max_s=max(times,default=None),other_devices_false_offline=sum(st['id'] not in ids and not st['online'] for _,st in off),decrypt_failures=hub._decrypt_fail_count,replays_dropped=hub._replay_count)
  result['passed']=all([result['all_online'],result['false_offlines']==0,missing==0,failures==0,result['restart_recovered_all'],len(detected())==len(ids),result['other_devices_false_offline']==0,result['decrypt_failures']==0])
 except Exception as exc:
  result.update(passed=False,error=f'{type(exc).__name__}: {exc}')
 finally:
  for d in devs:
   d.stop()
   if d.proc.stdout:d.proc.stdout.close()
  if hub:await hub.async_stop()
 return result

async def main():
 outputs=[]
 for count in COUNTS:
  print(json.dumps({'stage':'starting','devices':count}),flush=True)
  result=await run(count);outputs.append(result)
  (ROOT/f'result-{count}.json').write_text(json.dumps(result,indent=2)+'\n')
  (ROOT/'results.json').write_text(json.dumps(outputs,indent=2)+'\n')
  print(json.dumps({'stage':'complete',**result}),flush=True)
  if not result['passed']:
   print('Stopping ramp after a failed stage; inspect before increasing load',flush=True);break
 print(json.dumps({'finished':True,'passed_stages':sum(x['passed'] for x in outputs),'planned_stages':len(COUNTS)}),flush=True)

if __name__=='__main__':asyncio.run(main())
