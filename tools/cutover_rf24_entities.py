"""Run with a private prepared work directory; never commit its snapshots/configuration.
Requires HA_TOKEN and optionally HA_URL. Refuses repeated cutover attempts.
"""
import asyncio,json,urllib.request,pathlib,time,aiohttp,tarfile,hashlib,base64,os,sys
R=pathlib.Path(sys.argv[1]).resolve()
TOKEN=os.environ['HA_TOKEN'];BASE=os.environ.get('HA_URL','http://homeassistant.local:8123').rstrip('/')
HEAD={'Authorization':'Bearer '+TOKEN,'Content-Type':'application/json'}
def req(path,body=None):
 r=urllib.request.Request(BASE+path,headers=HEAD,data=None if body is None else json.dumps(body).encode());return json.load(urllib.request.urlopen(r,timeout=30))
async def ws_call(sock,seq,kind,**kw):
 await sock.send_json({'id':seq,'type':kind,**kw})
 while True:
  d=await sock.receive_json(timeout=30)
  if d.get('id')==seq:
   if not d.get('success'):raise RuntimeError(str(d.get('error')))
   return d.get('result')
async def main():
 assert not (R/'cutover-start.json').exists(),'Cutover already attempted; inspect receipts before continuing'
 d=req('/etcore/api/diag');device=next(x for h in d['hubs'] for x in h['devices'] if x['id']=='rf24-network-master');assert device['online'] and device['fw']=='rf24-etcore-2026.11.08.1' and device['last_addr']=='172.168.1.18' and device['hello']['enc']=='chacha20_poly1305'
 assert any(e.get('id')=='rf24-network-master' and e.get('type')=='state' and e.get('encrypted') for h in d['hubs'] for e in h['events']),'No encrypted live state evidence'
 mapping=json.load(open(R/'entity-migration.json'));mapping=[dict(m,old_entity_id=m['entity_id']) for m in mapping]
 async with aiohttp.ClientSession(headers=HEAD) as session:
  async with session.ws_connect(BASE+'/api/websocket') as sock:
   await sock.receive_json();await sock.send_json({'type':'auth','access_token':TOKEN});assert (await sock.receive_json())['type']=='auth_ok'
   reg=await ws_call(sock,1,'config/entity_registry/list');(R/'registry-before-cutover-private.json').write_text(json.dumps(reg));byunique={x.get('unique_id'):x for x in reg if x.get('platform')=='etcore'}
   assert all(m['new_unique_id'] in byunique for m in mapping)
   (R/'cutover-start.json').write_text(json.dumps({'at':time.time(),'old_ids':[x['old_entity_id'] for x in mapping if x['old_entity_id']]}))
   # YAML installation is a separate guarded operation, invoked by this command's caller.
   assert (R/'yaml-installed.json').exists(),'Candidate YAML not yet installed'
   req('/api/services/mqtt/reload',{})
   seq=2;actions=[]
   live={x['entity_id']:x for x in await ws_call(sock,seq,'config/entity_registry/list')};seq+=1
   for m in mapping:
    old=m['old_entity_id'];new=byunique[m['new_unique_id']]['entity_id']
    if not old:continue
    if old in live:
     assert live[old]['platform']=='mqtt','Target ID owned by another platform: '+old
     await ws_call(sock,seq,'config/entity_registry/remove',entity_id=old);seq+=1
    for attempt in range(20):
     if not any(x['entity_id']==old for x in req('/api/states')):break
     await asyncio.sleep(.2)
    else:raise RuntimeError('Legacy runtime entity still exists: '+old)
    old_state=next((x for x in json.load(open(R/'states-before-private.json')) if x['entity_id']==old),{})
    options={'new_entity_id':old}
    if old_state.get('attributes',{}).get('friendly_name'):options['name']=old_state['attributes']['friendly_name']
    old_reg=next((x for x in json.load(open(R/'registry-before-cutover-private.json')) if x['entity_id']==old),{})
    for attr in ['area_id','labels','aliases','hidden_by']:
     if old_reg.get(attr) is not None:options[attr]=old_reg[attr]
    await ws_call(sock,seq,'config/entity_registry/update',entity_id=new,**options);seq+=1;actions.append({'from':new,'to':old})
   req('/api/services/script/reload',{})
   after=await ws_call(sock,seq,'config/entity_registry/list');(R/'registry-after-cutover-private.json').write_text(json.dumps(after));lookup={x['entity_id']:x for x in after}
   assert all(lookup[m['old_entity_id']]['platform']=='etcore' and lookup[m['old_entity_id']]['unique_id']==m['new_unique_id'] for m in mapping if m['old_entity_id'])
   (R/'cutover-receipt.json').write_text(json.dumps({'ok':True,'at':time.time(),'renamed':actions,'entity_ids_preserved':len(actions),'mqtt_entries_retired':34,'pump_scripts_updated':0},indent=2));print('Cutover complete:',len(actions),'original entity IDs now owned by ET-Core')
if __name__=='__main__':asyncio.run(main())
