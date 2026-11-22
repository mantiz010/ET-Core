from pathlib import Path
import subprocess,re,json,hashlib,tempfile,atexit,shutil
p=Path(__file__).resolve().parents[2]/'arduino/libraries/ETCore/examples/HydroBucket'
r=Path(tempfile.mkdtemp(prefix='hydro-bucket-test-'))
atexit.register(shutil.rmtree,r)
s=(p/'HydroBucket.ino').read_text()
pre=r'''#include "BucketEcModel.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define EC_FILTER_SIZE 30
#define EC_OUTLIER_PERCENT 15
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
uint32_t clockMs=0;uint32_t millis(){return clockMs;}
float Temperature=25,ecLastCycles=NAN,ecLastTemperature=NAN,ec25_mS=0,ecNow_mS=0,ecFiltered=0,ec=0,cfFiltered=0,cf=0,cfMqttFiltered=0;
float ecReadings[30];int ecReadIndex=0;bool ecFilterInitialized=false,cfMqttInitialized=false,ecSampleValid=false;
uint32_t lastValidEcMs=0,ecGoodSamples=0,ecModelGeneration=0,ecFilterGeneration=0,ecPublishedGeneration=0;
float EC_ALPHA=BucketEC::SOLUTION_ALPHA,CF_COEF=10;
constexpr uint32_t EC_STALE_MS=20000;
BucketEC::Calibration ecCalibration=BucketEC::emptyCalibration();BucketEC::RawWindow ecRawWindow;
int cf_on=0,cf_DoseWait=0,cf_on_timeA=0,cf_on_timeB=0,cf_on_timeC=0;
bool cfDoseActive=false;constexpr int Cf_pumpA=32,Cf_pumpB=33,LOW=0;
int pinA=0,pinB=0;
void digitalWrite(int pin,int v){if(pin==Cf_pumpA)pinA=v;else if(pin==Cf_pumpB)pinB=v;}
unsigned checks=0;
#define check(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
// Independent forward fixture: fixed circuit resistance plus solution resistance.
// Values chosen independently of the user's tank readings.
float forwardCycles(float ec25,float t,float gain=300,float offset=80){return offset+gain/(ec25*(1+.02f*(t-25)));}
float forwardRaw(float ec25,float t,float gain=300,float offset=80){return forwardCycles(ec25,t,gain,offset)/(1+.02f*(t-25));}
BucketEC::Calibration exampleCalibration(float gain=300,float offset=80){
 auto c=BucketEC::emptyCalibration();
 check(BucketEC::capturePoint(c,1,forwardRaw(1.413f,10,gain,offset),10));
 check(!BucketEC::calibrated(c));
 check(BucketEC::capturePoint(c,2,forwardRaw(2.770f,30,gain,offset),30));
 check(BucketEC::calibrated(c));return c;
}
'''
fn=s[s.index('bool getFilteredEC(uint32_t raw) {'):s.index('// Never write an invalid NTP',s.index('bool getFilteredEC(uint32_t raw) {'))]
fn+=s[s.index('bool ecFresh() {'):s.index('void loadEcCalibration() {')]
post=r'''
int main(){
 using namespace BucketEC;
 auto c=emptyCalibration();float result=99,cycles=0;
 check(valid(c));check(!calibrated(c));check(!convert(300,25,c,result));check(result==99);
 // Original EEPROM points can be reused only with explicitly supplied temperatures.
 auto legacy=c;check(reuseLegacy(legacy,373,25,219,25));
 check(decodeTiming(373,25,cycles)&&convert(cycles,25,legacy,result));check(fabsf(result*10-14.13f)<.001f);
 check(decodeTiming(219,25,cycles)&&convert(cycles,25,legacy,result));check(fabsf(result*10-27.70f)<.001f);
 printf("Synthetic 25 C capture conditions: stored raw 373 -> 14.130 CF; 219 -> 27.700 CF. Actual original capture temperatures remain unknown.\n");
 auto unchanged=legacy;check(!reuseLegacy(legacy,373,NAN,219,25));check(memcmp(&legacy,&unchanged,sizeof(legacy))==0);
 check(!reuseLegacy(legacy,373,0,219,25));check(memcmp(&legacy,&unchanged,sizeof(legacy))==0);
 // Different standard temperatures, gain/offset combinations, true concentrations.
 for(float offset:{25.f,80.f,200.f})for(float gain:{180.f,300.f,500.f}){
  auto model=exampleCalibration(gain,offset);
  check(fabsf(model.gain-gain)<.01f&&fabsf(model.offset-offset)<.01f);
  for(float t:{5.f,10.f,15.f,25.f,30.f,40.f})for(float strength:{.2f,1.3f,2.6f,4.5f}){
   check(convert(forwardCycles(strength,t,gain,offset),t,model,result));check(fabsf(result-strength)<.001f);
  }
 }
 c=exampleCalibration();unchanged=c;
 // Partial replacement cannot silently destroy an existing calibrated pair.
 check(capturePoint(c,1,forwardRaw(1.413f,20,400,90),20));check(calibrated(c));
 check(c.gain==unchanged.gain&&c.offset==unchanged.offset&&c.pendingMask==1);
 auto partial=c;check(!capturePoint(c,2,500000,20));check(memcmp(&c,&partial,sizeof(c))==0);
 check(capturePoint(c,2,forwardRaw(2.770f,28,400,90),28));check(c.pendingMask==0&&fabsf(c.gain-400)<.01f);
 // Any one-bit corruption of the persisted record is rejected.
 for(size_t i=0;i<sizeof(c)*8;i++){
  auto damaged=c;reinterpret_cast<uint8_t*>(&damaged)[i/8]^=uint8_t(1u<<(i%8));check(!valid(damaged));
 }
 auto wrong=c;wrong.magic=0x45433501;seal(wrong);check(!valid(wrong));
 wrong=c;wrong.gain*=1.2f;seal(wrong);check(!valid(wrong));
 wrong=c;wrong.point14.ec25=1.6f;seal(wrong);check(!valid(wrong));
 float parsed;uint32_t raw;
 for(const char* bad:{"","nan","inf","0","50","25x","25,10","1e99"})check(!parseTemperature(bad,parsed));
 check(parseTemperature("25.3",parsed)&&fabsf(parsed-25.3f)<.001f);
 for(const char* bad:{"0,25","255,nan","255,50","255,25x","255,25,3","-3,25","42949672960,25","255,"})check(!parseSample(bad,raw,parsed));
 check(parseSample("255,25.6",raw,parsed)&&raw==255);
 // Capture bootstrap while calibrated EC is unavailable; stale/unstable refused.
 RawWindow window;Point point;
 for(unsigned i=0;i<8;i++){check(window.push(300+(i%2),25,4000*i));if(i<7)check(!window.stablePoint(4000*i,point));}
 check(window.stablePoint(28000,point));check(point.senderRaw==300||point.senderRaw==301);
 check(!window.stablePoint(35000,point));
 check(window.push(350,25,32000));check(!window.stablePoint(32000,point));
 window.reset();for(unsigned i=0;i<8;i++)check(window.push(300,25+(i%2),4000*i));
 check(!window.stablePoint(28000,point));
 window.reset();for(unsigned i=0;i<8;i++)check(window.push(300,25,100*i));
 check(!window.stablePoint(700,point));
 window.reset();for(unsigned i=0;i<8;i++)check(window.push(300,25,uint32_t(0xffffc000u+4000*i)));
 check(window.stablePoint(uint32_t(0xffffc000u+28000),point));
 check(!window.push(0,25,30000));check(!window.freshRaw(30000));
 // Actual production code: no default/tank-fit measurement; raw stream remains
 // available to perform calibration when the EC conversion is not qualified.
 for(unsigned i=0;i<8;i++){clockMs+=4000;check(!getFilteredEC(300));invalidateEC();}
 check(ecRawFresh());check(!ecFresh());check(ecRawWindow.stablePoint(clockMs,point));
 ecCalibration=exampleCalibration();ecModelGeneration++;
 for(float t:{10.f,25.f,5.f,40.f,10.f}){
  Temperature=t;
  for(unsigned i=0;i<40;i++){
   clockMs+=4000;check(getFilteredEC(uint32_t(forwardRaw(1.3f,t))));check(fabsf(cf-13)<.12f);
  }
 }
 check(ecFresh());
 for(unsigned i=0;i<60;i++){clockMs+=4000;check(getFilteredEC(uint32_t(forwardRaw(2.6f,Temperature))));}
 check(cf>25.9f&&cf<26.2f&&cfMqttFiltered>25.8f);
 // No misleading fixed target: replacing the true calibration scale immediately
 // resets old EC samples and MQTT EMA; three qualified samples still required.
 ecCalibration=exampleCalibration(400,80);ecModelGeneration++;ecSampleValid=false;
 clockMs+=4000;check(getFilteredEC(uint32_t(forwardRaw(2.6f,Temperature))));
 check(cf>34.5f&&cf<35.0f&&fabsf(cfMqttFiltered-cf)<.001f);check(!ecFresh());
 for(unsigned i=0;i<2;i++){clockMs+=4000;check(getFilteredEC(uint32_t(forwardRaw(2.6f,Temperature))));}
 check(ecFresh());ecModelGeneration++;check(!ecFresh());ecModelGeneration--;
 clockMs+=20001;check(!ecFresh());check(!ecRawFresh());
 cf_on=1;cf_DoseWait=1;cfDoseActive=true;pinA=pinB=1;cf_on_timeA=cf_on_timeB=cf_on_timeC=10;
 invalidateEC();check(!cf_on&&!cf_DoseWait&&!cfDoseActive&&!pinA&&!pinB&&!cf_on_timeA&&!cf_on_timeB&&!cf_on_timeC);
 check(!ecFresh());invalidateEcRaw();check(!ecRawFresh());
 printf("PASS: %u checks; two-point conversion, missing calibration, capture temperatures, nonzero offsets, thermal sweeps, concentration changes, CRC corruption, stable raw capture, production filters/freshness/output shutdown.\n",checks);
}
'''
f=r/'test_model_and_production.cpp';f.write_text(pre+fn+post)
q=subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),str(f),'-o',str(r/'test_model_and_production')],text=True,capture_output=True);print(q.stderr);q.check_returncode()
q=subprocess.run([str(r/'test_model_and_production')],text=True,capture_output=True)
(r/'MODEL_TEST_RESULTS.txt').write_text(q.stdout+q.stderr);print(q.stdout,q.stderr);q.check_returncode()
