#pragma once
#define ESP_IMAGE_HEADER_MAGIC 0xe9
struct esp_image_header_t{uint8_t magic,chip_id,spi_size;uint8_t unused[21];};
