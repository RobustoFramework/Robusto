#pragma once
#include <robconfig.h>

#define UNIT_TESTING 1


#ifdef ARDUINO
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#endif

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#ifdef ARDUINO_ARCH_MBED
REDIRECT_STDOUT_TO(Serial);
#endif


#include <unity.h>

/** 
 * For native dev-platform or for some embedded frameworks \
 */ 
#if !(defined(ARDUINO) || defined(ESP_PLATFORM)) 
#define TEST_ENTRY_POINT(run_func) \
int main(void) { \
    run_func(NULL); \
    return 0; \
} \

#endif  

/**  
 * For Arduino framework \
 */ 
#if defined(ARDUINO) 
#define TEST_ENTRY_POINT(run_func) \
void setup() { \
    xTaskCreate((TaskFunction_t) &run_func, "Test task",384, NULL, 1, NULL); \
} \
\
void loop() {} \
\

#endif 



/** 
 * For ESP-IDF framework 
 */ 

#ifdef ESP_PLATFORM 
#define TEST_ENTRY_POINT(run_func) \
    void app_main() { \
        vTaskDelay(1000/portTICK_PERIOD_MS); \
        run_func(NULL); \
    }
#endif 
