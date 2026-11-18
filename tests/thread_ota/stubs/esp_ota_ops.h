#pragma once
#include <vector>
#include <cstdint>
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 2
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffe
using esp_err_t=int;using esp_ota_handle_t=uint32_t;
struct esp_partition_t{size_t size;};
enum esp_ota_img_states_t{ESP_OTA_IMG_PENDING_VERIFY,ESP_OTA_IMG_VALID,ESP_OTA_IMG_UNDEFINED};
inline esp_partition_t run{1310720},slot{1310720};
inline std::vector<uint8_t> flashBytes;inline int opened=0,ended=0,aborted=0,activated=0,rolledBack=0;
inline int failure=0;inline esp_ota_img_states_t bootState=ESP_OTA_IMG_VALID;
inline const esp_partition_t* esp_ota_get_running_partition(){return &run;}
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*){return failure==1?nullptr:failure==2?&run:&slot;}
inline int esp_ota_begin(const esp_partition_t* p,size_t n,esp_ota_handle_t* h){assert(p==&slot&&n==OTA_WITH_SEQUENTIAL_WRITES);if(failure==3)return 1;opened++;*h=42;flashBytes.clear();return 0;}
inline int esp_ota_write(esp_ota_handle_t h,const void* p,size_t n){assert(h==42);if(failure==4)return 1;auto b=(const uint8_t*)p;flashBytes.insert(flashBytes.end(),b,b+n);return 0;}
inline int esp_ota_end(esp_ota_handle_t h){assert(h==42);ended++;return failure==5?1:0;}
inline int esp_ota_abort(esp_ota_handle_t){aborted++;return 0;}
inline int esp_ota_set_boot_partition(const esp_partition_t* p){assert(p==&slot);activated++;return failure==6?1:0;}
inline int esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t* s){*s=bootState;return failure==7?ESP_ERR_NOT_FOUND:0;}
inline int esp_ota_mark_app_valid_cancel_rollback(){bootState=ESP_OTA_IMG_VALID;return 0;}
inline int esp_ota_mark_app_invalid_rollback_and_reboot(){rolledBack++;return 0;}
