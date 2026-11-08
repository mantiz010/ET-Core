"""Local SMS history and durable send deduplication. No gateway credentials."""
import json
import re
import sqlite3
import time
import uuid

GSM = set("@£$¥èéùìòÇ\nØø\rÅåΔ_ΦΓΛΩΠΨΣΘΞÆæßÉ !\"#¤%&'()*+,-./0123456789:;<=>?¡ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÑÜ§¿abcdefghijklmnopqrstuvwxyzäöñüà")
EXT = set('^{}\\[~]|€')


def measure(message):
    if all(c in GSM or c in EXT for c in message):
        units=sum(2 if c in EXT else 1 for c in message)
        return {'encoding':'GSM-7','units':units,'limit':160,'parts':1 if units<=160 else (units+152)//153}
    units=len(message.encode('utf-16-be'))//2
    return {'encoding':'Unicode','units':units,'limit':70,'parts':1 if units<=70 else (units+66)//67}


def validate_message(message):
    if not isinstance(message,str) or not message.strip() or len(message)>1000:
        raise ValueError('Write a message first (maximum one SMS).')
    if any(ord(c)<32 and c not in '\n\r' for c in message):
        raise ValueError('Remove unsupported control characters.')
    if not message.isascii() or any(ord(c)<32 for c in message) or not all(c in GSM or c in EXT for c in message):
        raise ValueError('This gateway uses plain-text SMS. Replace emoji and non-ASCII characters.')
    info=measure(message)
    if info['parts']>1:
        raise ValueError(f"Keep this to one SMS: {info['units']}/{info['limit']} {info['encoding']} units.")
    return message


class History:
    def __init__(self,path):
        self.db=sqlite3.connect(path)
        self.db.row_factory=sqlite3.Row
        self.db.executescript('''PRAGMA journal_mode=WAL;
        CREATE TABLE IF NOT EXISTS messages (
          id TEXT PRIMARY KEY, at REAL NOT NULL, direction TEXT NOT NULL,
          message TEXT NOT NULL, number TEXT NOT NULL DEFAULT '',
          status TEXT NOT NULL, source TEXT NOT NULL, observed INTEGER DEFAULT 0,
          request_id TEXT UNIQUE, detail TEXT NOT NULL DEFAULT '');
        CREATE INDEX IF NOT EXISTS messages_at ON messages(at);
        CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY,value TEXT NOT NULL);
        ''')
        self.db.executescript('''CREATE TABLE IF NOT EXISTS native_refs (identity TEXT PRIMARY KEY,message_id TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS native_results (request_id TEXT PRIMARY KEY,status TEXT NOT NULL,at REAL NOT NULL);''')
        # A killed process must not resend a claimed request on next startup.
        self.db.execute("UPDATE messages SET status='unconfirmed',detail='App restarted during send. Do not assume failure; no automatic retry.' WHERE status='sending'")
        self.db.commit()

    def get(self,key,default=None):
        row=self.db.execute('SELECT value FROM settings WHERE key=?',(key,)).fetchone()
        return json.loads(row[0]) if row else default

    def set(self,key,value):
        self.db.execute('INSERT OR REPLACE INTO settings VALUES (?,?)',(key,json.dumps(value)))
        self.db.commit()

    def prune(self):
        self.db.execute('DELETE FROM messages WHERE at<?',(time.time()-30*86400,))
        self.db.execute('DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY at DESC LIMIT 2000)')
        self.db.commit()

    def row(self,ident):
        row=self.db.execute('SELECT * FROM messages WHERE id=?',(ident,)).fetchone()
        return dict(row) if row else None

    def claim(self,message,request_id,now=None):
        now=time.time() if now is None else now
        if not isinstance(request_id,str) or not re.fullmatch(r'[a-zA-Z0-9-]{16,80}',request_id):
            raise ValueError('Missing send identifier. Reload the page.')
        message=validate_message(message)
        previous=self.db.execute('SELECT * FROM messages WHERE request_id=?',(request_id,)).fetchone()
        if previous:
            if previous['message']!=message:raise ValueError('This send identifier belongs to a different message.')
            return dict(previous),False
        previous=self.db.execute("SELECT * FROM messages WHERE source='composer' AND message=? AND at>? ORDER BY at DESC LIMIT 1",(message,now-120)).fetchone()
        if previous:return dict(previous),False
        last=self.db.execute("SELECT max(at) FROM messages WHERE source='composer'").fetchone()[0]
        if last and now-last<5:raise ValueError('Wait a few seconds before sending another message.')
        ident=uuid.uuid4().hex
        self.db.execute("INSERT INTO messages(id,at,direction,message,status,source,request_id) VALUES (?,?,'out',?,'sending','composer',?)",(ident,now,message,request_id))
        self.db.commit()
        return self.row(ident),True

    def complete(self,ident,status,detail):
        row=self.row(ident)
        if row['observed']:
            status=row['status'];detail=row['detail']
        self.db.execute('UPDATE messages SET status=?,detail=? WHERE id=?',(status,detail,ident))
        self.db.commit()
        return self.row(ident)

    def observe(self,direction,payload,retained=False):
        # Retained commands are never treated as fresh traffic or replayed.
        if retained:return
        if isinstance(payload,bytes):payload=payload.decode('utf-8','replace')
        if len(payload)>16384:return
        try:data=json.loads(payload)
        except (ValueError,TypeError):data={}
        if not isinstance(data,dict):data={}
        message=str(data.get('message') or payload)[:4000]
        number=str(data.get('sender') if direction=='in' else data.get('number') or '')[:80]
        if direction=='in' and number=='None':number=''
        now=time.time()
        if direction=='out' and not number:
            pending=self.db.execute("SELECT id FROM messages WHERE source='composer' AND direction='out' AND message=? AND observed=0 AND at>? ORDER BY at LIMIT 1",(message,now-120)).fetchone()
            if pending:
                self.db.execute("UPDATE messages SET observed=1,status='queued',detail='Observed on outgoing MQTT queue. Delivery unconfirmed.' WHERE id=?",(pending[0],))
                self.db.commit();return
        self.db.execute('INSERT INTO messages(id,at,direction,message,number,status,source,observed,detail) VALUES (?,?,?,?,?,?,?,?,?)',
                        (uuid.uuid4().hex,now,direction,message,number,'received' if direction=='in' else 'queued','bridge',1,
                         'Received from SMS bridge.' if direction=='in' else 'Observed on outgoing MQTT queue. Delivery unconfirmed.'))
        self.db.commit();self.prune()

    def native_incoming(self, data):
        ident=data.get('event_id');message=data.get('message');number=data.get('sender')
        if not isinstance(ident,str) or len(ident)>100 or not ident or not isinstance(message,str) or not isinstance(number,str):return
        identity='in:'+ident
        if self.db.execute('SELECT 1 FROM native_refs WHERE identity=?',(identity,)).fetchone():return
        rowid=uuid.uuid4().hex
        self.db.execute("INSERT INTO messages(id,at,direction,message,number,status,source,observed,detail) VALUES (?,?,'in',?,?,'received','etcore',1,'Received through encrypted ET-Core.')",(rowid,time.time(),message[:4000],number[:80]))
        self.db.execute('INSERT INTO native_refs VALUES (?,?)',(identity,rowid));self.db.commit();self.prune()

    def native_outgoing(self, state):
        attrs=state.get('attributes') or {};ident=attrs.get('request_id');message=state.get('state')
        if attrs.get('transport')!='etcore' or not isinstance(ident,str) or not re.fullmatch(r'[0-9a-f-]{36}',ident) or not isinstance(message,str):return
        identity='out:'+ident
        ref=self.db.execute('SELECT message_id FROM native_refs WHERE identity=?',(identity,)).fetchone()
        if ref:rowid=ref[0]
        else:
            pending=self.db.execute("SELECT id FROM messages WHERE source='composer' AND message=? AND observed=0 AND at>? ORDER BY at LIMIT 1",(message,time.time()-120)).fetchone()
            if pending:rowid=pending[0]
            else:
                rowid=uuid.uuid4().hex
                self.db.execute("INSERT INTO messages(id,at,direction,message,number,status,source,observed) VALUES (?,?,'out',?,?,'queued','etcore',1)",(rowid,time.time(),message,str(attrs.get('number') or '')))
            self.db.execute('INSERT INTO native_refs VALUES (?,?)',(identity,rowid))
        self.db.execute('UPDATE messages SET observed=1 WHERE id=?',(rowid,));self.db.commit()
        result=self.db.execute('SELECT status FROM native_results WHERE request_id=?',(ident,)).fetchone()
        self.native_result({'request_id':ident,'status':result[0] if result else attrs.get('submission_status','queued')})

    def native_result(self, data):
        ident=data.get('request_id');status=data.get('status')
        details={
            'submitted_to_gateway':'Submitted to encrypted gateway; acceptance unconfirmed.',
            'queued':'Gateway queued the SMS. Carrier and handset delivery are unconfirmed.',
            'modem_accepted':'Modem accepted the SMS. Handset delivery remains unconfirmed.',
            'submission_unknown':'Modem submission is uncertain. No automatic retry; retrying may duplicate the SMS.',
            'not_submitted':'Gateway did not submit this SMS.'}
        if not isinstance(ident,str) or not re.fullmatch(r'[0-9a-f-]{36}',ident) or status not in details:return
        prior=self.db.execute('SELECT status FROM native_results WHERE request_id=?',(ident,)).fetchone()
        if prior and prior[0] in ('modem_accepted','submission_unknown','not_submitted') and status in ('queued','submitted_to_gateway'):status=prior[0]
        self.db.execute('INSERT OR REPLACE INTO native_results VALUES (?,?,?)',(ident,status,time.time()))
        ref=self.db.execute('SELECT message_id FROM native_refs WHERE identity=?',('out:'+ident,)).fetchone()
        if ref:self.db.execute('UPDATE messages SET status=?,detail=? WHERE id=?',(status,details[status],ref[0]))
        self.db.execute('DELETE FROM native_results WHERE at<?',(time.time()-30*86400,))
        self.db.execute('DELETE FROM native_refs WHERE message_id NOT IN (SELECT id FROM messages)')
        self.db.commit()

    def list(self):
        self.prune()
        return [dict(r) for r in self.db.execute('SELECT * FROM messages ORDER BY at DESC LIMIT 500')]
