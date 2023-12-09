#include "test_log.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <stdio.h>

void robusto_test_log(char a) {
    Serial.write(a);
    Serial.flush();
}
#endif