#pragma once

// === I2S Microphone (INMP441) ===
#define MIC_I2S_PORT     I2S_NUM_0
#define MIC_I2S_BCK      GPIO_NUM_14
#define MIC_I2S_WS       GPIO_NUM_15
#define MIC_I2S_DIN      GPIO_NUM_32
#define MIC_SAMPLE_RATE  16000
#define MIC_BITS         16
#define MIC_CHANNELS     1

// === I2S Speaker (MAX98357A) ===
#define SPK_I2S_PORT     I2S_NUM_1
#define SPK_I2S_BCK      GPIO_NUM_26
#define SPK_I2S_WS       GPIO_NUM_25
#define SPK_I2S_DOUT     GPIO_NUM_22
#define SPK_SAMPLE_RATE  16000

// === DMA Buffers ===
#define DMA_BUF_LEN      512
#define DMA_BUF_COUNT     4

// === SPI Display (GC9A01 240x240) ===
#define DISP_SPI_HOST    SPI2_HOST
#define DISP_PIN_MOSI    GPIO_NUM_23
#define DISP_PIN_SCLK    GPIO_NUM_18
#define DISP_PIN_CS      GPIO_NUM_5
#define DISP_PIN_DC      GPIO_NUM_16
#define DISP_PIN_RST     GPIO_NUM_17
#define DISP_PIN_BL      GPIO_NUM_4
#define DISP_WIDTH       240
#define DISP_HEIGHT      240

// === I2C (MPU6050) ===
#define I2C_PORT         I2C_NUM_0
#define I2C_SDA          GPIO_NUM_21
#define I2C_SCL          GPIO_NUM_19
#define I2C_FREQ         400000
#define MPU6050_ADDR     0x68

// === Battery ADC ===
#define BATT_ADC_PIN     GPIO_NUM_34
#define BATT_DIVIDER_R1  100000  // 100K ohm
#define BATT_DIVIDER_R2  100000  // 100K ohm
#define BATT_FULL_MV     4200
#define BATT_EMPTY_MV    3300

// === FreeRTOS Task Priorities ===
#define TASK_PRIO_AUDIO     23
#define TASK_PRIO_DISPLAY   10
#define TASK_PRIO_SENSOR     8
#define TASK_PRIO_NETWORK    5

// === FreeRTOS Stack Sizes ===
#define STACK_AUDIO      8192
#define STACK_DISPLAY    8192
#define STACK_SENSOR     4096
#define STACK_NETWORK    8192

// === Wi-Fi ===
#define WIFI_MAX_RETRY   10
#define WS_SERVER_URI    "ws://192.168.1.100:8000/audio"

// === Wake Word ===
#define WAKE_WORD        "미니야"

// === Power Management ===
#define SLEEP_TIMEOUT_MS     300000   // 5 min idle -> light sleep
#define LOW_BATT_THRESHOLD   20      // percent
#define CRIT_BATT_THRESHOLD  5       // percent

// === OTA ===
#define OTA_CHECK_URL    "http://192.168.1.100:8000/api/firmware/version"
