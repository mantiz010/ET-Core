import sys,unittest,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[2]/'homeassistant/apps/sms_control_center'))
from store import History
RID='01234567-89ab-cdef-0123-456789abcdef'
class NativeTests(unittest.TestCase):
 def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.path=Path(self.tmp.name)/'db';self.h=History(self.path)
 def tearDown(self):self.h.db.close();self.tmp.cleanup()
 def state(self,status='queued'):
  return {'state':'Hello','attributes':{'transport':'etcore','request_id':RID,'submission_status':status}}
 def test_incoming_dedup_across_restart(self):
  e={'event_id':'aabbccddeeff-1','sender':'+64000000','message':'Hello'};self.h.native_incoming(e);self.h.db.close();self.h=History(self.path);self.h.native_incoming(e);self.assertEqual(len(self.h.list()),1)
 def test_composer_merge_and_final_result(self):
  r,_=self.h.claim('Hello','a'*32);self.h.native_outgoing(self.state());self.h.native_result({'request_id':RID,'status':'modem_accepted'})
  row=self.h.complete(r['id'],'accepted','service accepted');self.assertEqual(len(self.h.list()),1);self.assertEqual(row['status'],'modem_accepted')
 def test_result_before_state_and_no_regression(self):
  self.h.native_result({'request_id':RID,'status':'modem_accepted'});self.h.native_outgoing(self.state());self.h.native_result({'request_id':RID,'status':'queued'});self.assertEqual(self.h.list()[0]['status'],'modem_accepted')
 def test_outgoing_reconnect_no_duplicate(self):
  self.h.native_outgoing(self.state());self.h.db.close();self.h=History(self.path);self.h.native_outgoing(self.state());self.assertEqual(len(self.h.list()),1)
 def test_legacy_history_kept(self):
  self.h.observe('in','Legacy text');self.h.native_outgoing(self.state());self.assertEqual(len(self.h.list()),2)
if __name__=='__main__':unittest.main()
