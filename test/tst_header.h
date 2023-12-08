#pragma once
#include <robconfig.h>

#define UNIT_TESTING 1


#ifdef USE_ARDUINO
#include <Arduino.h>
#include <FreeRTOS.h>

#endif

#ifdef USE_ESPIDF
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
#if !(defined(USE_ARDUINO) || defined(USE_ESPIDF)) 
#define TEST_ENTRY_POINT(run_func) \
int main(void) { \
    run_func(NULL); \
    return 0; \
} \

#endif  

/**  
 * For Arduino framework \
 */ 
#if defined(USE_ARDUINO) 
#define TEST_ENTRY_POINT(run_func) \
void setup() { \
    xTaskCreate((TaskFunction_t) &run_func, "Test task", 8192, NULL, 8, NULL); \
} \
\
void loop() {} \
\

#endif 



/** 
 * For ESP-IDF framework 
 */ 

#ifdef USE_ESPIDF 
#define TEST_ENTRY_POINT(run_func) \
    void app_main() { \
        vTaskDelay(1000/portTICK_PERIOD_MS); \
        run_func(NULL); \
    }
#endif 
