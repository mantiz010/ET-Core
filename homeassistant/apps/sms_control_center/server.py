"""Private Ingress SMS app. Observes native HA SMS events; sends only through the existing HA service."""
import asyncio
import contextlib
import json
import logging
import os
from pathlib import Path
import time
import uuid

import aiohttp
from aiohttp import web
from store import History, measure

VERSION='2.1.0'
STATIC=Path(__file__).parent/'static'
LOG=logging.getLogger('sms_control_center')
INCOMING='home/sms/incoming'
OUTGOING='home/sms/send'
FIRMWARE='home/sms/gateway/firmware'


class Center:
    def __init__(self,folder):
        folder.mkdir(parents=True,exist_ok=True)
        self.history=History(folder/'sms.sqlite3')
        self.token=os.environ.get('SUPERVISOR_TOKEN') or os.environ.get('HA_TOKEN','')
        self.base=os.environ.get('HA_API','http://supervisor/core/api').rstrip('/')
        self.service_url=os.environ.get('MQTT_SERVICE_URL','http://supervisor/services/mqtt')
        self.session=None
        self.send_lock=asyncio.Lock()
        self.connected=False
        self.connection_note='Connecting to message history feed…'
        self.last_event=None
        self.firmware=self.history.get('firmware','Not observed yet')

    async def request(self,path):
        async with self.session.get(path,headers={'Authorization':'Bearer '+self.token}) as r:
            r.raise_for_status();return await r.json()

    def observe_event(self, event):
        kind=event.get('event_type');data=event.get('data') or {}
        if kind=='sms_control_center_incoming':
            self.history.native_incoming(data)
        elif kind=='sms_control_center_result':
            self.history.native_result(data)
        elif kind=='state_changed' and data.get('entity_id')=='sensor.sms_control_center_last_outgoing':
            self.history.native_outgoing(data.get('new_state') or {})
        else:return
        self.last_event=time.time()

    async def listen(self):
        while True:
            try:
                url=self.base.removesuffix('/api')+'/websocket'
                async with self.session.ws_connect(url,heartbeat=30) as ws:
                    await ws.receive_json()
                    await ws.send_json({'type':'auth','access_token':self.token})
                    if (await ws.receive_json()).get('type')!='auth_ok':raise ValueError('HA authentication failed')
                    for ident,event in enumerate(['sms_control_center_incoming','sms_control_center_result','state_changed'],1):
                        await ws.send_json({'id':ident,'type':'subscribe_events','event_type':event})
                        while True:
                            result=await ws.receive_json()
                            if result.get('type')=='event':self.observe_event(result.get('event') or {});continue
                            if result.get('id')==ident:break
                        if not result.get('success'):raise ValueError('Event subscription failed')
                    self.connected=True
                    self.connection_note='Listening to Home Assistant ET-Core SMS events. Gateway readiness is checked on send.'
                    self.firmware='Encrypted ET-Core SMS gateway'
                    # Reconcile the last outgoing request/status after an app reconnect.
                    state=await self.request(self.base+'/states/sensor.sms_control_center_last_outgoing')
                    self.history.native_outgoing(state)
                    async for msg in ws:
                        if msg.type==aiohttp.WSMsgType.TEXT:
                            value=json.loads(msg.data)
                            if value.get('type')=='event':self.observe_event(value.get('event') or {})
                        elif msg.type in (aiohttp.WSMsgType.CLOSED,aiohttp.WSMsgType.ERROR):break
            except asyncio.CancelledError:raise
            except Exception as exc:
                LOG.warning('HA SMS history feed disconnected (%s)',type(exc).__name__)
            finally:
                self.connected=False;self.connection_note='HA SMS history feed disconnected. Messages during this gap may be missing.'
            await asyncio.sleep(10)

    async def send(self,body):
        if not isinstance(body,dict):raise ValueError('Invalid message request.')
        async with self.send_lock:
            if not self.connected:raise ValueError('The message feed is disconnected. Wait for it to reconnect before sending.')
            row,fresh=self.history.claim(body.get('message'),body.get('request_id'))
            if not fresh:return {'record':row,'duplicate':True}
            try:
                async with self.session.post(self.base+'/services/sms_control_center/send_sms',headers={'Authorization':'Bearer '+self.token},json={'message':row['message']}) as r:
                    if 200<=r.status<300:
                        status,detail='accepted','Accepted by Home Assistant SMS service; awaiting modem result. Handset delivery is unconfirmed.'
                    elif r.status in (400,401,403,404):
                        status,detail='rejected','Home Assistant rejected the send request. Check the SMS integration.'
                    else:
                        status,detail='unconfirmed','Service returned an error. Message may have been queued; not retried.'
            except (aiohttp.ClientError,TimeoutError):
                status,detail='unconfirmed','Request timed out or disconnected. Message may have been queued; not retried.'
            return {'record':self.history.complete(row['id'],status,detail),'duplicate':False}

    def status(self):
        rows=self.history.list()
        # Counts cover the displayed history, not invented lifetime totals.
        return {'version':VERSION,'connected':self.connected,'connection_note':self.connection_note,
                'firmware':self.firmware,'last_event':self.last_event,'messages':rows,
                'counts':{'incoming':sum(r['direction']=='in' for r in rows),
                          'outgoing':sum(r['direction']=='out' for r in rows),
                          'attention':sum(r['status'] in ('rejected','unconfirmed','submission_unknown','not_submitted') for r in rows)},
                'recipient':'Your configured mobile','retention_days':30}


def make_app(center,dev=False):
    @web.middleware
    async def guard(request,handler):
        if request.path!='/health':
            allowed={'127.0.0.1','::1'} if dev else {'172.30.32.2'}
            if request.remote not in allowed:raise web.HTTPForbidden(text='Open this app through Home Assistant.')
            if request.method=='POST' and (request.headers.get('X-Requested-With')!='SMSCenter' or request.content_type!='application/json'):
                raise web.HTTPForbidden(text='Invalid app request.')
        response=await handler(request)
        response.headers['Content-Security-Policy']="default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; frame-ancestors 'self'; base-uri 'none'; form-action 'self'"
        response.headers['X-Content-Type-Options']='nosniff'
        response.headers['Referrer-Policy']='no-referrer'
        response.headers['Cache-Control']='no-store'
        return response
    app=web.Application(middlewares=[guard],client_max_size=8192)
    async def health(request):return web.json_response({'status':'running','version':VERSION})
    async def status(request):return web.json_response(center.status())
    async def send(request):
        try:
            body=await request.json()
            return web.json_response(await center.send(body))
        except (ValueError,UnicodeError) as exc:raise web.HTTPBadRequest(text=str(exc)) from None
    async def export(request):
        return web.json_response({'exported_at':time.time(),'messages':center.history.list()},headers={'Content-Disposition':'attachment; filename="sms-history-private.json"'})
    async def static(request):
        name=request.match_info.get('name','index.html')
        if name not in ('index.html','app.js','style.css'):raise web.HTTPNotFound()
        return web.FileResponse(STATIC/name)
    async def lifecycle(app):
        center.session=aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=20,connect=5))
        task=asyncio.create_task(center.listen())
        yield
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):await task
        await center.session.close();center.history.db.close()
    app.cleanup_ctx.append(lifecycle)
    app.router.add_get('/health',health)
    app.router.add_get('/api/status',status)
    app.router.add_post('/api/send',send)
    app.router.add_get('/api/export',export)
    app.router.add_get('/',static)
    app.router.add_get('/{name}',static)
    return app

if __name__=='__main__':
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(levelname)s %(message)s')
    center=Center(Path(os.environ.get('DATA_DIR','/data')))
    dev=os.environ.get('SMS_CENTER_DEV')=='1'
    web.run_app(make_app(center,dev),host='127.0.0.1' if dev else '0.0.0.0',port=int(os.environ.get('PORT',8099)),access_log=None)
