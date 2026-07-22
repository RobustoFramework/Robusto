#include "robusto_c6_update_protocol.h"

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
    CHECK(ROBUSTO_C6_UPDATER_REVISION_LEGACY == 1U);
    CHECK(ROBUSTO_C6_UPDATER_REVISION_CURRENT == 2U);
    CHECK(ROBUSTO_C6_UPDATE_COMMAND_ABORT == 5U);
    CHECK(sizeof(robusto_c6_update_request_t) == 56U);
    CHECK(sizeof(robusto_c6_update_response_t) == 28U);
    CHECK(offsetof(robusto_c6_update_request_t, transaction_id) == 8U);
    CHECK(offsetof(robusto_c6_update_request_t, sha256) == 24U);
    CHECK(offsetof(robusto_c6_update_response_t, status) == 12U);
    CHECK(offsetof(robusto_c6_update_response_t, updater_revision) == 22U);
    CHECK(offsetof(robusto_c6_update_response_t, total_size) == 24U);

    if (failures == 0) {
        printf("C6 update protocol tests passed: %d assertions\n", assertions);
    }
    return failures == 0 ? 0 : 1;
}