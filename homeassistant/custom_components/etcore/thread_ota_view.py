"""Admin-only, bounded image review and explicit installation API."""
import asyncio
import json
from aiohttp import web
from homeassistant.components.http import HomeAssistantView, KEY_HASS_USER
from .const import DOMAIN
from .thread_ota import ThreadOTA, MAX_IMAGE

class ThreadOTAView(HomeAssistantView):
    url='/etcore/api/thread_ota/{entry_id}/{dev_id}/{action}'
    name='etcore:thread_ota'
    requires_auth=True
    max_body_size=MAX_IMAGE+65536

    async def post(self,request,entry_id,dev_id,action):
        user=request.get(KEY_HASS_USER)
        if not user or not user.is_admin:raise web.HTTPForbidden()
        hub=(request.app['hass'].data.get(DOMAIN) or {}).get(entry_id)
        if hub is None or dev_id not in hub.devices:raise web.HTTPNotFound()
        if not getattr(hub,'thread_ota',None):
            raise web.HTTPServiceUnavailable(reason="OTA manager is not ready")
        manager=hub.thread_ota
        try:
            if action not in ('prepare','install'):raise web.HTTPNotFound()
            if action=='prepare':
                if request.content_type!='application/octet-stream':raise ValueError('Select an application BIN')
                body=bytearray()
                async def read():
                    async for chunk in request.content.iter_chunked(65536):
                        if len(body)+len(chunk)>MAX_IMAGE:raise ValueError('Image exceeds upload limit')
                        body.extend(chunk)
                await asyncio.wait_for(read(),120)
                result=manager.prepare(dev_id,bytes(body),request.query.get('board',''),request.query.get('version',''),user.id)
            else:
                if request.content_type!='application/json':raise ValueError('Expected an installation confirmation')
                body=bytearray()
                async for chunk in request.content.iter_chunked(1024):
                    if len(body)+len(chunk)>1024:raise ValueError('Confirmation exceeds limit')
                    body.extend(chunk)
                p=json.loads(body)
                if not isinstance(p,dict) or p.get('confirm') is not True:raise ValueError('Confirm the reviewed image')
                result=manager.install(dev_id,p.get('plan_id'),p.get('sha256'),user.id)
            return web.json_response(result,status=202 if action=='install' else 200,headers={'Cache-Control':'no-store'})
        except (ValueError,TypeError,UnicodeError,KeyError,asyncio.TimeoutError) as exc:
            # Messages are fixed validator text, never firmware contents or credentials.
            return web.json_response({'error':str(exc) if isinstance(exc,ValueError) else 'Invalid update request'},status=400)
