#pragma once
#include "ETCoreOTA.h"
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <mbedtls/sha256.h>
#include <sdkconfig.h>

// Writes ONLY the inactive OTA app partition. Never touches bootloader/NVS.
class ETCoreOTAESP32 : public ETCoreOTASink {
public:
 ETCoreOTAESP32(){mbedtls_sha256_init(&_sha);}
 ~ETCoreOTAESP32(){abort();mbedtls_sha256_free(&_sha);}
 size_t capacity() const override {auto p=esp_ota_get_next_update_partition(nullptr);return p&&p!=esp_ota_get_running_partition()?p->size:0;}
 bool begin(size_t size,const char* hash) override {
  abort();_partition=esp_ota_get_next_update_partition(nullptr);
  if(!_partition||_partition==esp_ota_get_running_partition()||size>_partition->size)return false;
  snprintf(_expected,sizeof(_expected),"%s",hash);_size=size;_written=0;_verified=false;
  if(mbedtls_sha256_starts(&_sha,0))return false;
  if(esp_ota_begin(_partition,OTA_WITH_SEQUENTIAL_WRITES,&_handle)!=ESP_OK)return false;
  _open=true;return true;
 }
 bool write(const uint8_t* data,size_t size) override {
  if(!_open||size>_size-_written)return false;
  if(!_written){
   if(size<sizeof(esp_image_header_t))return false;
   esp_image_header_t header;memcpy(&header,data,sizeof(header));
   if(header.magic!=ESP_IMAGE_HEADER_MAGIC||header.chip_id!=CONFIG_IDF_FIRMWARE_CHIP_ID||header.spi_size>7||((uint64_t)1024*1024<<header.spi_size)>ESP.getFlashChipSize())return false;
  }
  if(esp_ota_write(_handle,data,size)!=ESP_OK||mbedtls_sha256_update(&_sha,data,size))return false;
  _written+=size;return true;
 }
 bool finish() override {
  if(!_open||_written!=_size)return false;
  uint8_t digest[32];char hex[65];if(mbedtls_sha256_finish(&_sha,digest))return false;
  for(size_t i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
  if(strcmp(hex,_expected)){abort();return false;}
  esp_err_t result=esp_ota_end(_handle);_open=false;
  _verified=result==ESP_OK;return _verified;
 }
 bool activate() override {return _verified&&esp_ota_set_boot_partition(_partition)==ESP_OK;}
 void abort() override {if(_open)esp_ota_abort(_handle);_open=false;_verified=false;}
 void restart() override {ESP.restart();}
 bool confirmHealthy() override {
  esp_ota_img_states_t state;
  auto running=esp_ota_get_running_partition();
  esp_err_t status=esp_ota_get_state_partition(running,&state);
  if(status==ESP_ERR_NOT_FOUND){_healthConfirmed=true;return true;}
  if(status!=ESP_OK)return false;
  if(state==ESP_OTA_IMG_PENDING_VERIFY){
   if(esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK)return false;
  }else if(state!=ESP_OTA_IMG_VALID && state!=ESP_OTA_IMG_UNDEFINED)return false;
  _healthConfirmed=true;return true;
 }
 void poll() override {
  if(_healthConfirmed||_rollbackRequested)return;
  esp_ota_img_states_t state;
  if(esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK
     &&state==ESP_OTA_IMG_PENDING_VERIFY && millis()>180000){
   _rollbackRequested=true;
   esp_ota_mark_app_invalid_rollback_and_reboot();
  }
 }
private:
 const esp_partition_t* _partition=nullptr;esp_ota_handle_t _handle=0;
 mbedtls_sha256_context _sha;bool _open=false,_verified=false,_healthConfirmed=false,_rollbackRequested=false;
 size_t _size=0,_written=0;char _expected[65]={};
};
