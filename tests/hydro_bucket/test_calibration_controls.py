from pathlib import Path
import subprocess,re,json,hashlib,tempfile,atexit,shutil
p=Path(__file__).resolve().parents[2]/'arduino/libraries/ETCore/examples/HydroBucket'
r=Path(tempfile.mkdtemp(prefix='hydro-bucket-test-'))
atexit.register(shutil.rmtree,r)
s=(p/'HydroBucket.ino').read_text()
pre=r'''#include "BucketEcModel.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define F(x) (x)
struct String {
 std::string s;String()=default;String(const char*x):s(x){}String(std::string x):s(x){}
 size_t length()const{return s.size();}const char*c_str()const{return s.c_str();}
 int indexOf(const char*x)const{auto n=s.find(x);return n==std::string::npos?-1:int(n);}
 long toInt()const{return std::strtol(s.c_str(),nullptr,10);}
 bool operator==(const char*x)const{return s==x;}
};
String getParam(const String &h,const char* key){
 std::string k=std::string(key)+"=";auto i=h.s.find(k);if(i==std::string::npos)return {};
 i+=k.size();auto e=h.s.find_first_of("& ",i);return h.s.substr(i,e==std::string::npos?e:e-i);
}
struct WiFiClient{std::string out;void println(const char*x){out+=x;out+='\n';}void println(){out+='\n';}};
struct DebugType{void println(const char*){}}Debug;
std::vector<unsigned char> stored;bool storePresent=false,writeFail=false,beginFail=false;unsigned writes=0;
struct Preferences {
 bool begin(const char*ns,bool ro){return std::string(ns)=="bucket_ec6"&&!beginFail&&(!ro||storePresent);}
 bool isKey(const char*){return storePresent;}
 size_t getBytesLength(const char*){return stored.size();}
 size_t getBytes(const char*,void*dest,size_t n){if(n!=stored.size())return 0;memcpy(dest,stored.data(),n);return n;}
 size_t putBytes(const char*,const void*src,size_t n){++writes;if(writeFail)return 0;const auto*b=(const unsigned char*)src;stored.assign(b,b+n);storePresent=true;return n;}
 void end(){}
};
uint32_t nowMs=0;uint32_t millis(){return nowMs;}
bool maintenanceMode=false,otaInProgress=false,ecSampleValid=false,ecStorageInvalid=false;
uint32_t ecModelGeneration=0,ecRaw14=373,ecRaw27=219;
int cf_dose_count=0,ph_dose_count=0,cf_dose_max=30,ph_dose_max=10;
BucketEC::Calibration ecCalibration=BucketEC::emptyCalibration();BucketEC::RawWindow ecRawWindow;
unsigned checks=0;
#define check(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
void stable(unsigned raw){ecRawWindow.reset();for(unsigned i=0;i<8;i++){nowMs+=4000;ecRawWindow.push(raw,25,nowMs);}}
'''
fn=s[s.index('void loadEcCalibration() {'):]
post=r'''
int main(){
 using namespace BucketEC;
 loadEcCalibration();check(!calibrated(ecCalibration));check(!ecStorageInvalid);
 WiFiClient reply;
 check(handleEcCalibration("GET /?ec_capture=1 HTTP/1.1",reply));check(reply.out.find("409 Conflict")!=std::string::npos);check(writes==0);
 maintenanceMode=true;reply={};
 check(handleEcCalibration("GET /?ec_capture=1 HTTP/1.1",reply));check(reply.out.find("409 Conflict")!=std::string::npos);check(writes==0);
 // Missing calibration does NOT deadlock capture: only stable raw/T is needed.
 stable(373);reply={};check(handleEcCalibration("GET /?ec_capture=1 HTTP/1.1",reply));
 check(reply.out.find("200 OK")!=std::string::npos);check(writes==1&&!calibrated(ecCalibration)&&ecCalibration.pendingMask==1);
 check(ecRawWindow.count==0);check(ecCalibration.pending14.temperature==25);
 // Pending point survives reboot, and a second fresh window activates the pair.
 ecCalibration=emptyCalibration();loadEcCalibration();check(ecCalibration.pendingMask==1&&!calibrated(ecCalibration));
 reply={};check(handleEcCalibration("GET /?ec_capture=2 HTTP/1.1",reply));check(reply.out.find("409 Conflict")!=std::string::npos);
 stable(219);reply={};check(handleEcCalibration("GET /?ec_capture=2 HTTP/1.1",reply));check(reply.out.find("200 OK")!=std::string::npos);
 check(calibrated(ecCalibration)&&ecCalibration.pendingMask==0&&writes==2);check(ecRaw14==373&&ecRaw27==219);
 auto saved=ecCalibration;ecCalibration=emptyCalibration();loadEcCalibration();check(memcmp(&saved,&ecCalibration,sizeof(saved))==0);
 float result;check(convert(373,25,ecCalibration,result)&&fabsf(result*10-14.13f)<.001f);
 // A failed write cannot change the active RAM calibration or generation.
 stable(373);writeFail=true;unsigned generation=ecModelGeneration;reply={};
 check(handleEcCalibration("GET /?ec_capture=1 HTTP/1.1",reply));check(reply.out.find("409 Conflict")!=std::string::npos);
 check(memcmp(&saved,&ecCalibration,sizeof(saved))==0&&generation==ecModelGeneration);writeFail=false;
 // No one-point tank shortcut; blank legacy temperatures are not 25 C defaults.
 unsigned count=writes;reply={};check(handleEcCalibration("GET /?ec_reference_cf=16 HTTP/1.1",reply));check(reply.out.find("409 Conflict")!=std::string::npos&&writes==count);
 reply={};check(handleEcCalibration("GET /?ec_legacy_temp14=&ec_legacy_temp27= HTTP/1.1",reply));check(reply.out.find("409 Conflict")!=std::string::npos&&writes==count);
 reply={};check(handleEcCalibration("GET /?ec_legacy_temp14=25&ec_legacy_temp27=nan HTTP/1.1",reply));check(reply.out.find("409 Conflict")!=std::string::npos&&writes==count);
 reply={};check(handleEcCalibration("GET /?ec_legacy_temp14=25&ec_legacy_temp27=25 HTTP/1.1",reply));check(reply.out.find("200 OK")!=std::string::npos);
 check(calibrated(ecCalibration)&&ecRaw14==373&&ecRaw27==219);
 // Corruption fails closed; the user can explicitly recapture after that fault.
 stored[12]^=1;ecCalibration=emptyCalibration();loadEcCalibration();check(ecStorageInvalid&&!calibrated(ecCalibration));
 stable(373);reply={};check(handleEcCalibration("GET /?ec_capture=1 HTTP/1.1",reply));check(reply.out.find("200 OK")!=std::string::npos&&!ecStorageInvalid);
 check(!calibrated(ecCalibration)&&ecCalibration.pendingMask==1);
 // OTA and maintenance gates always take precedence over captures.
 stable(219);otaInProgress=true;count=writes;reply={};check(handleEcCalibration("GET /?ec_capture=2 HTTP/1.1",reply));check(reply.out.find("409 Conflict")!=std::string::npos&&writes==count);otaInProgress=false;
 // Bookkeeping restore works even before EC is calibrated; it never drives pins.
 reply={};check(handleEcCalibration("GET /?restore_cf_count=17&restore_ph_count=1 HTTP/1.1",reply));check(reply.out.find("200 OK")!=std::string::npos&&cf_dose_count==17&&ph_dose_count==1);
 maintenanceMode=false;reply={};check(handleEcCalibration("GET /?restore_cf_count=18 HTTP/1.1",reply));check(reply.out.find("409 Conflict")!=std::string::npos&&cf_dose_count==17);
 check(!handleEcCalibration("GET /data HTTP/1.1",reply));
 printf("PASS: %u extracted-handler/storage checks; bootstrap calibration, pending-point reboot, full-pair reboot, write failure, corrupted storage, no single-point shortcut, explicit legacy temperatures, OTA/maintenance gates, counter restore.\n",checks);
}
'''
f=r/'test_actual_calibration_controls.cpp';f.write_text(pre+fn+post)
q=subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),str(f),'-o',str(r/'test_actual_calibration_controls')],capture_output=True,text=True);print(q.stderr);q.check_returncode()
q=subprocess.run([str(r/'test_actual_calibration_controls')],capture_output=True,text=True);(r/'CONTROL_TEST_RESULTS.txt').write_text(q.stdout+q.stderr);print(q.stdout,q.stderr);q.check_returncode()
# Extract emitted web JavaScript verbatim and syntax-check it without a device flash.
a=s.index('client.println("<script>");');b=s.index('</script>',a)
js=[]
for line in s[a:b].splitlines()[1:]:
 m=re.search(r'client\.println\(("(?:\\.|[^"\\])*")\);',line)
 if m:js.append(json.loads(m[1]))
f=r/'candidate-web-ui.js';f.write_text('\n'.join(js));q=subprocess.run(['node','--check',str(f)],capture_output=True,text=True);(r/'UI_CHECK.json').write_text(json.dumps({'ok':q.returncode==0,'stderr':q.stderr},indent=2));print(q.stderr);q.check_returncode();print('Candidate web UI JavaScript syntax passed.')
