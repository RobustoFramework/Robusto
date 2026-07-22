#include "robusto_c6_recovery_protocol.h"

#include <stddef.h>
#include <stdio.h>

static int assertions;
static int failures;

#define CHECK(condition) check((condition), #condition, __LINE__)

static void check(int condition, const char *expression, int line)
{
    ++assertions;
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    }
}

int main(void)
{
    CHECK(ROBUSTO_C6_RECOVERY_IDENTITY_VERSION == 1U);
    CHECK(ROBUSTO_C6_RECOVERY_PROTOCOL_REVISION == 1U);
    CHECK(ROBUSTO_C6_RECOVERY_FRONTEND_ESP_HOSTED == 1U);
    CHECK(ROBUSTO_C6_RECOVERY_FRONTEND_RAW_SDIO == 2U);
    CHECK(ROBUSTO_C6_RECOVERY_TARGET_ESP32_C6 == 1U);
    CHECK(sizeof(robusto_c6_recovery_request_t) == 40U);
    CHECK(sizeof(robusto_c6_recovery_record_t) == 45U);
    CHECK(offsetof(robusto_c6_recovery_record_t, generation) == 6U);
    CHECK(offsetof(robusto_c6_recovery_record_t, partition_subtype) == 11U);
    CHECK(offsetof(robusto_c6_recovery_record_t, build_sha256) == 13U);

    if (failures == 0) {
        printf("C6 recovery protocol tests passed: %d assertions\n", assertions);
    }
    return failures == 0 ? 0 : 1;
}