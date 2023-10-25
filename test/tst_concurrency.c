#include "tst_concurrency.h"

// TODO: Add copyrights on all these files
#include <unity.h>

#if !(defined(USE_ARDUINO) || defined(USE_ESPIDF))
#include <time.h>
#include <stdio.h>
#endif


#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>
#include <robusto_concurrency.h>
#include <robusto_time.h>
#include <robusto_init.h>

bool task_test;

void task_running(int *task_done) {
    ROB_LOGI("Test", "In task_running");

    *task_done = 999;
    robusto_delete_current_task();
}


/**
 * @brief Check multitasking
 */
void tst_task(void) {
    ROB_LOGI("Test", "In test_task");
    int task_done = 0;
    rob_task_handle_t *task_handle;
    char task_name[30] = "test_task";
    r_delay(100);
    robusto_create_task((TaskFunction_t)task_running, &task_done, task_name , &task_handle,0);

    int starttime = r_millis();
    while (r_millis() < (starttime + 100)) {
        if (task_done == 999) {       
            TEST_ASSERT_TRUE(true);
            return;
        }
        r_delay(10);
    }
    TEST_ASSERT_TRUE(false);

}
