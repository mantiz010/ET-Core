import asyncio
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import AsyncMock
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'homeassistant/apps/sms_control_center'))
from store import History,measure,validate_message
from server import Center,make_app
from aiohttp import web
from aiohttp.test_utils import TestClient,TestServer

class HistoryTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.path=Path(self.tmp.name)/'db';self.h=History(self.path)
    def tearDown(self):self.h.db.close();self.tmp.cleanup()
    def test_gsm_extension_and_unicode_boundaries(self):
        self.assertEqual(measure('^'*80)['units'],160)
        validate_message('A'*160);validate_message('^'*80);self.assertEqual(measure('😀'*35)['units'],70)
        for s in ['^'*81,'A'*161,'😀'*36,'😀','café','`','','\x00']:
            with self.assertRaises(ValueError):validate_message(s)
    def test_claim_restart_no_duplicate(self):
        r,new=self.h.claim('Test','a'*32);self.assertTrue(new)
        self.h.db.close();self.h=History(self.path)
        again,new=self.h.claim('Test','a'*32);self.assertFalse(new)
        self.assertEqual(again['id'],r['id']);self.assertEqual(again['status'],'unconfirmed')
    def test_same_identifier_cannot_change_message(self):
        self.h.claim('One','a'*32)
        with self.assertRaises(ValueError):self.h.claim('Two','a'*32)
    def test_two_tabs_same_text_and_rate_limit(self):
        r,new=self.h.claim('One','a'*32,1000)
        same,new=self.h.claim('One','b'*32,1001)
        self.assertFalse(new);self.assertEqual(same['id'],r['id'])
        with self.assertRaises(ValueError):self.h.claim('Two','c'*32,1002)
    def test_mqtt_echo_merges_without_false_delivery(self):
        r,_=self.h.claim('One','a'*32)
        self.h.observe('out',json.dumps({'message':'One'}))
        self.h.complete(r['id'],'accepted','Accepted')
        rows=self.h.list();self.assertEqual(len(rows),1);self.assertEqual(rows[0]['status'],'queued')
        self.h.observe('out',json.dumps({'message':'One'}))
        self.assertEqual(len(self.h.list()),2) # A separate actual publish remains visible.
    def test_external_number_does_not_attach_to_default_recipient(self):
        r,_=self.h.claim('One','a'*32)
        self.h.observe('out',json.dumps({'message':'One','number':'+64123456789'}))
        self.assertEqual(len(self.h.list()),2);self.assertEqual(self.h.row(r['id'])['status'],'sending')
    def test_retained_commands_not_replayed_or_recorded(self):
        self.h.observe('out',json.dumps({'message':'turn off lights'}),True)
        self.assertEqual(self.h.list(),[])
    def test_inbound_and_plain_text_survive_restart(self):
        self.h.observe('in',b'{"message":"Hello","sender":"+64123456789"}')
        self.h.observe('out','Plain text')
        self.h.db.close();self.h=History(self.path)
        rows=self.h.list();self.assertEqual(len(rows),2)
        self.assertEqual(rows[1]['status'],'received');self.assertEqual(rows[1]['number'],'+64123456789')
    def test_old_records_pruned(self):
        self.h.claim('Old','a'*32,time.time()-31*86400)
        self.assertEqual(self.h.list(),[])

class ApiTests(unittest.IsolatedAsyncioTestCase):
    async def test_ingress_and_csrf(self):
        with tempfile.TemporaryDirectory() as d:
            center=Center(Path(d));center.listen=AsyncMock()
            async with TestClient(TestServer(make_app(center))) as c:
                self.assertEqual((await c.get('/health')).status,200)
                self.assertEqual((await c.get('/api/status',headers={'X-Forwarded-For':'172.30.32.2'})).status,403)
    async def test_send_only_fixed_service_dedup_and_errors(self):
        calls=[];response_status=200
        async def receive(request):
            calls.append((request.path,await request.json()))
            return web.json_response([],status=response_status)
        mock=web.Application();mock.router.add_post('/services/sms_control_center/send_sms',receive)
        async with TestServer(mock) as ha:
            with tempfile.TemporaryDirectory() as d:
                center=Center(Path(d));center.listen=AsyncMock();center.base=str(ha.make_url('')).rstrip('/')
                async with TestClient(TestServer(make_app(center,True))) as c:
                    center.connected=True
                    headers={'X-Requested-With':'SMSCenter'}
                    payload={'message':'Test route','request_id':'a'*32,'number':'ignored','topic':'ignored'}
                    self.assertEqual((await c.post('/api/send',json=payload)).status,403)
                    responses=await asyncio.gather(*(c.post('/api/send',json=payload,headers=headers) for _ in range(2)))
                    bodies=[await r.json() for r in responses]
                    self.assertEqual(len(calls),1);self.assertEqual(calls[0],('/services/sms_control_center/send_sms',{'message':'Test route'}))
                    self.assertEqual(bodies[0]['record']['status'],'accepted');self.assertTrue(bodies[1]['duplicate'])
                    self.assertEqual((await c.post('/api/send',json=[],headers=headers)).status,400)
                    self.assertEqual((await c.get('/server.py')).status,404)
                    center.connected=False
                    self.assertEqual((await c.post('/api/send',json={'message':'Other','request_id':'b'*32},headers=headers)).status,400)
                    self.assertEqual(len(calls),1)
                    export=await c.get('/api/export');self.assertEqual(export.status,200)
                    self.assertIn('private.json',export.headers['Content-Disposition'])
    async def test_service_errors_do_not_trigger_retry(self):
        for code,expected in [(403,'rejected'),(500,'unconfirmed')]:
            calls=[]
            async def receive(request):calls.append(1);return web.Response(status=code)
            mock=web.Application();mock.router.add_post('/services/sms_control_center/send_sms',receive)
            async with TestServer(mock) as ha:
                with tempfile.TemporaryDirectory() as d:
                    center=Center(Path(d));center.listen=AsyncMock();center.base=str(ha.make_url('')).rstrip('/')
                    async with TestClient(TestServer(make_app(center,True))) as c:
                        center.connected=True
                        p={'message':'Test error','request_id':'a'*32};h={'X-Requested-With':'SMSCenter'}
                        first=await (await c.post('/api/send',json=p,headers=h)).json()
                        await c.post('/api/send',json=p,headers=h)
                        self.assertEqual(first['record']['status'],expected);self.assertEqual(len(calls),1)

if __name__=='__main__':unittest.main()
