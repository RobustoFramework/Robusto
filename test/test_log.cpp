#include "test_log.h"
#include <Arduino.h>
#include <stdio.h>

void robusto_test_log(char a) {
    Serial.write(a);
}