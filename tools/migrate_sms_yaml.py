from pathlib import Path
import json,re,yaml
import argparse
parser=argparse.ArgumentParser(description='Convert legacy SMS MQTT actions/triggers; writes to a separate directory.')
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
assert args.source.resolve()!=args.output.resolve(), 'Use a different output directory; keep originals as backup'
args.output.mkdir(parents=True,exist_ok=True)
r=args.output
counts={'sends':0,'incoming':0}
def decode_payload(s):
 blocks=[]
 def hide(m):blocks.append(m.group());return f'ETBLOCK{len(blocks)-1}X'
 encoded=re.sub(r'{{.*?}}|{%.*?%}',hide,s,flags=re.S)
 d=json.loads(encoded)
 for k,v in d.items():
  for i,b in enumerate(blocks):v=v.replace(f'ETBLOCK{i}X',b)
  d[k]=v
 return d

def walk(x, incoming=False):
 if isinstance(x,dict):
  if x.get('service',x.get('action'))=='mqtt.publish' and x.get('data',{}).get('topic') in ('home/sms/send','home/sms/send2'):
   data=x['data'];body=decode_payload(data['payload']);assert 'message' in body
   x.pop('action',None);x['service']='sms_control_center.send_sms';x['data']={**body,'topic':data['topic']};counts['sends']+=1
  if x.get('platform',x.get('trigger'))=='mqtt' and x.get('topic')=='home/sms/incoming':
   assert set(x).issubset({'platform','trigger','topic','id'}),set(x)
   x.pop('trigger',None);x.pop('topic');x['platform']='event';x['event_type']='sms_control_center_incoming';counts['incoming']+=1
  for k,v in x.items():
   if isinstance(v,str) and incoming:x[k]=v.replace('trigger.payload_json','trigger.event.data')
   else:walk(v,incoming)
 elif isinstance(x,list):
  for v in x:walk(v,incoming)
for name in ['automations.yaml','scripts.yaml']:
 d=yaml.safe_load((args.source/name).read_text());
 for a in (d if isinstance(d,list) else d.values()):
  incoming='home/sms/incoming' in str(a)
  walk(a,incoming)
 (r/name).write_text(yaml.safe_dump(d,sort_keys=False,allow_unicode=True))
print('Migration counts',counts)
(r/'yaml-migration-counts.json').write_text(json.dumps(counts))
