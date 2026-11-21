"""Bounded, authenticated negotiated-window OTA; no automatic update initiation.

The caller must authorize a specific image for a specific device. Chunks use
normal encrypted commands/ACKs. A new matching boot plus an authenticated health
ACK is required for success. Interrupted transfers are abandoned, never resumed
or retried automatically after HA restarts. Receiver aborts after 60s silence.
"""
from __future__ import annotations
import asyncio
import base64
import hashlib
import re
import secrets
from .ota_image import inspect_image
import time
from typing import Any

PROTOCOL = 'etcore-chunk-v1'
MAX_IMAGE = 4 * 1024 * 1024
CHUNK = 192
FAST_CHUNK = 288
MAX_WINDOW = 2
# Physical 288/window2 test failed; keep automatic installs on the proven profile.
# This switch is for controlled regression testing, not a user-facing speed claim.
ENABLE_WINDOWED_PREVIEW = False
TOKEN = re.compile(r'[A-Za-z0-9_.+-]{1,64}\Z')
ACTIVE = {'preparing', 'transferring', 'verifying', 'rebooting', 'checking_health'}


def validate_image(image: bytes, *, board: str, version: str, capability: dict, current: str | None) -> dict:
    if not TOKEN.fullmatch(version): raise ValueError('Invalid target version')
    if version == current: raise ValueError('Target version is already installed')
    if capability.get('ready') is not True or capability.get('protocol') != PROTOCOL:
        raise ValueError('This device does not have the ET-Core OTA receiver')
    # First reviewed target. Never infer safety for a different board from chip alone.
    if board != 'esp32c6-dev-4m' or capability.get('board') != board:
        raise ValueError('Firmware board does not match the receiver')
    maximum=capability.get('max_chunk');window=capability.get('max_window',1)
    if type(maximum) is not int or maximum not in (CHUNK,FAST_CHUNK):raise ValueError('Unsupported OTA chunk size')
    if type(window) is not int or not 1<=window<=MAX_WINDOW:raise ValueError('Unsupported OTA window')
    limit = capability.get('max_image_size')
    if type(limit) is not int or not 32 <= len(image) <= min(limit, MAX_IMAGE):
        raise ValueError('Application image does not fit the inactive OTA partition')
    info=inspect_image(image)
    if info.chip_id!=13 or info.flash_bytes>4*1024*1024:
        raise ValueError('Select an ESP32-C6 application BIN for 4MB flash')
    if (version.encode()+b'\0' not in image or board.encode()+b'\0' not in image
            or PROTOCOL.encode()+b'\0' not in image):
        raise ValueError('Image must contain the reviewed version, board and OTA receiver')
    return {'board':board,'version':version,'size':len(image),'sha256':hashlib.sha256(image).hexdigest(),'chunk_size':maximum if ENABLE_WINDOWED_PREVIEW else CHUNK,'window':window if ENABLE_WINDOWED_PREVIEW else 1}


class ThreadOTA:
    def __init__(self, hub, persist=None):
        self.hub=hub
        self.persist=persist
        self.jobs: dict[str, dict] = {}
        self.tasks: dict[str, asyncio.Task] = {}
        self.waiters: dict[str, tuple[str, asyncio.Future]] = {}
        self.plan = None
        self.plan_timer = None
        hub.register_listener(self.receive)

    async def save(self):
        if self.persist:await self.persist(self.jobs)

    def restore(self,saved):
        if not isinstance(saved,dict):return
        for dev_id,job in list(saved.items())[:1000]:
            if not isinstance(dev_id,str) or not isinstance(job,dict):continue
            if not isinstance(job.get('phase'),str) or job['phase'] not in ACTIVE|{'succeeded','failed','interrupted','unconfirmed','rolled_back'}:continue
            if job.get('phase') in ACTIVE:
                job=dict(job,phase='interrupted',error='HA restarted; verify the running version before retrying')
            self.jobs[dev_id]=job
            if dev_id in self.hub.devices:self.hub.devices[dev_id]['ota_job']=job

    def receive(self, msg: dict) -> None:
        if not msg.get('_encrypted') or msg.get('_replay'): return
        dev_id=msg.get('id')
        if dev_id in self.jobs and dev_id in self.hub.devices:self.hub.devices[dev_id]['ota_job']=self.jobs[dev_id]
        p=msg.get('payload') or {}
        job=self.jobs.get(dev_id)
        if job and msg.get('type')=='hello' and job.get('commit_sent') and job['phase'] in {'unconfirmed','interrupted'}:
            session=(self.hub.devices.get(dev_id) or {}).get('rx_session')
            trial=job.get('trial_session')
            # Older jobs only recorded this rejection after the matching trial boot's health command.
            trial_observed=trial or (job['phase']=='unconfirmed' and job.get('error')=='Receiver rejected the update command')
            if (trial_observed and session and job.get('original_session') and session not in (trial,job['original_session'])
                    and job.get('original_version') and p.get('fw')==job['original_version'] and p['fw']!=job.get('version')):
                job.update(phase='rolled_back',rollback_session=session,rollback_at=time.time())
                self.hub.hass.async_create_task(self.save())
        if msg.get('type')=='ack':
            if p.get('err')=='stale':return  # hub repairs its command counter and resends
            pair=self.waiters.get(str(p.get('cmd_id') or ''))
            if pair and pair[0]==msg.get('id') and not pair[1].done():
                pair[1].set_result(p.get('ok') is True)

    def prepare(self, dev_id, image, board, version, owner):
        if any(not t.done() for t in self.tasks.values()):raise ValueError('An update is active')
        dev=self.hub.devices.get(dev_id) or {}
        if not self.hub._psk or not dev.get('online') or not dev.get('rx_session'):
            raise ValueError('An online encrypted receiver is required')
        if (dev.get('hello') or {}).get('transport')!='thread':raise ValueError('Select a Thread receiver')
        metadata=validate_image(image,board=board,version=version,capability=(dev.get('hello') or {}).get('ota') or {},current=dev.get('fw'))
        plan_id=secrets.token_hex(16)
        self.plan={'plan_id':plan_id,'device':dev_id,'owner':owner,'image':image,
                   'session':dev['rx_session'],'original_version':dev.get('fw'),**metadata}
        if self.plan_timer:self.plan_timer.cancel()
        self.plan_timer=self.hub.hass.loop.call_later(300,self.expire_plan)
        return {'plan_id':plan_id,'device':dev_id,'original_version':dev.get('fw'),**metadata}

    def expire_plan(self):
        self.plan=None
        self.plan_timer=None

    def install(self,dev_id,plan_id,sha256,owner):
        p=self.plan
        if not p or p['device']!=dev_id or p['plan_id']!=plan_id or p['owner']!=owner or p['sha256']!=sha256:
            raise ValueError('Review this image before installing it')
        dev=self.hub.devices.get(dev_id) or {}
        if dev.get('rx_session')!=p['session'] or dev.get('fw')!=p['original_version']:
            raise ValueError('Device changed after review; review again')
        job=self.start(dev_id,p['image'],p['board'],p['version'])
        if self.plan_timer:self.plan_timer.cancel()
        self.expire_plan()
        return job

    def start(self, dev_id: str, image: bytes, board: str, version: str) -> dict:
        dev=self.hub.devices.get(dev_id) or {}
        if not self.hub._psk or not dev.get('online') or not dev.get('rx_session'):
            raise ValueError('An online authenticated device is required')
        hello=dev.get('hello') or {}
        if hello.get('transport')!='thread': raise ValueError('This preview supports Thread test receivers only')
        if dev_id in self.tasks and not self.tasks[dev_id].done(): raise ValueError('An update is already active')
        if any(not task.done() for task in self.tasks.values()): raise ValueError('Another update is active on this connection')
        manifest=validate_image(image,board=board,version=version,capability=hello.get('ota') or {},current=dev.get('fw'))
        job={'transfer':secrets.token_hex(16),'phase':'preparing','progress':0,'bytes':0,
             'started':time.time(),'original_session':dev['rx_session'],'original_version':dev.get('fw'),**manifest}
        previous=self.jobs.get(dev_id)
        if previous and previous.get('transfer'):
            fields=('transfer','phase','progress','started','finished','original_version','version','error','rollback_at')
            summary={key:previous[key] for key in fields if key in previous}
            job['history']=[summary,*previous.get('history',[])[:18]]
        self.jobs[dev_id]=job;dev['ota_job']=job
        self.tasks[dev_id]=self.hub.hass.loop.create_task(self._run(dev_id,image,job))
        return dict(job)

    async def command(self, dev_id: str, payload: dict) -> None:
        future=self.hub.hass.loop.create_future()
        cmd=self.hub.send_command(dev_id,'firmware.ota',payload)
        if not cmd: raise RuntimeError('Device is not reachable')
        self.waiters[cmd]=(dev_id,future)
        try:
            if not await asyncio.wait_for(future,8): raise RuntimeError('Receiver rejected the update command')
        finally:
            self.waiters.pop(cmd,None)
            pending=getattr(self.hub,'_pending',{}).pop(cmd,None)
            if pending and pending.get('timer'):pending['timer'].cancel()

    async def _run(self, dev_id: str, image: bytes, job: dict) -> None:
        transfer=job['transfer']
        try:
            await self.save()
            await asyncio.wait_for(self._transfer(dev_id,image,job),1800)
        except asyncio.CancelledError:
            job.update(phase='interrupted',error='Update interrupted; receiver timeout or boot verification required')
            raise
        except Exception as exc:
            job.update(phase='unconfirmed' if job.get('commit_sent') else 'failed',error=str(exc) or type(exc).__name__)
            if not job.get('commit_sent'):
                try:await self.command(dev_id,{'op':'abort','transfer':transfer})
                except Exception:pass
        finally:
            job.setdefault('finished',time.time())
            try:await self.save()
            except Exception:job['persistence_error']='Firmware history could not be saved'

    async def _transfer(self,dev_id,image,job):
        transfer=job['transfer']
        await self.command(dev_id,{'op':'begin','transfer':transfer,'size':job['size'],'sha256':job['sha256'],'board':job['board'],'chunk_size':job['chunk_size'],'window':job['window']})
        job['phase']='transferring';await self.save()
        saved_progress=-10
        chunk_size=job['chunk_size'];window=job['window']
        for offset in range(0,len(image),chunk_size*window):
            dev=self.hub.devices.get(dev_id) or {}
            if dev.get('rx_session')!=job['original_session']: raise RuntimeError('Receiver restarted during transfer')
            batch=[(position,image[position:position+chunk_size]) for position in range(offset,min(offset+chunk_size*window,len(image)),chunk_size)]
            results=await asyncio.gather(*(self.command(dev_id,{'op':'chunk','transfer':transfer,'offset':position,'data':base64.b64encode(chunk).decode()}) for position,chunk in batch),return_exceptions=True)
            # Settle the entire window before advancing or aborting; no earlier
            # negative ACK is hidden by a later positive one.
            for result in results:
                if isinstance(result,BaseException):raise result
            job['bytes']=offset+sum(len(chunk) for _,chunk in batch)
            job['progress']=min(99,job['bytes']*100//len(image))
            if job['progress']-saved_progress>=10:
                await self.save();saved_progress=job['progress']
        job['phase']='verifying';await self.save()
        await self.command(dev_id,{'op':'finish','transfer':transfer})
        # The commit may reach the device even if its ACK is lost.
        # From here onward, never report an abort as guaranteed safe.
        job['commit_sent']=True;job['phase']='rebooting';await self.save()
        try:await self.command(dev_id,{'op':'commit','transfer':transfer})
        except (RuntimeError,asyncio.TimeoutError):pass
        deadline=self.hub.hass.loop.time()+120
        while self.hub.hass.loop.time()<deadline:
            dev=self.hub.devices.get(dev_id) or {}
            if dev.get('online') and dev.get('rx_session')!=job['original_session']:
                if dev.get('fw')!=job['version']: raise RuntimeError('Unexpected firmware after reboot; inspect rollback status')
                job.update(phase='checking_health',trial_session=dev['rx_session'])
                await self.save()
                await self.command(dev_id,{'op':'health'})
                job.update(phase='succeeded',progress=100,finished=time.time());return
            await asyncio.sleep(.5)
        job.update(phase='unconfirmed',error='No verified new boot; inspect device before retrying')

    async def close(self):
        if self.plan_timer:self.plan_timer.cancel()
        self.expire_plan()
        tasks=[task for task in self.tasks.values() if not task.done()]
        for task in tasks:task.cancel()
        if tasks:await asyncio.gather(*tasks,return_exceptions=True)
        listeners=getattr(self.hub,'_message_listeners',[])
        if self.receive in listeners:listeners.remove(self.receive)
