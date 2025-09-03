// TODO: Add copyrights on all these files
#include "tst_concurrency.h"

#include <unity.h>
#ifdef USE_ESPIDF
#include <time.h>
#include <stdio.h>
#endif


#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>
#include "sample_queue.h"

int poll_count = 0;
int work_value = 0;

void sample_work_callback(void *queue_item) {
    work_value = ((sample_queue_item_t *)queue_item)->test_value;
}

void sample_poll_callback(void *q_context) {

    poll_count++;
    
}

/**
 * @brief Check multitasking
 */
void tst_queue_start(void) {
    ROB_LOGI("Test", "In tst_queue_start");

    sample_init_worker((sample_queue_callback *)&sample_work_callback, sample_poll_callback, "sample_queue");
    ROB_LOGI("Test", "Worker started");
    TEST_ASSERT_TRUE(true);
}

void tst_queue_check_poll(void) {
    ROB_LOGI("Test", "In tst_queue_check_poll");

    int starttime = r_millis();
    while (r_millis() < (starttime + 500)) {
        ROB_LOGI("Test", "Poll count %i", poll_count);
        if (poll_count > 0) {       
            TEST_ASSERT_TRUE(true);
            poll_count = 0;
            return;
        }
        
        r_delay(10);
    }
    TEST_ASSERT_TRUE(false);

}

void tst_queue_add_work(void) {
    ROB_LOGI("Test", "In tst_queue_add_work");

    sample_set_queue_blocked(false);
    ROB_LOGI("Test", "Queue unblocked");
    sample_safe_add_work_queue(999);
    ROB_LOGI("Test", "Work added");
    TEST_ASSERT_TRUE(true);

}

void tst_queue_check_work(void) {
    ROB_LOGI("Test", "In tst_queue_check_work");

    int starttime = r_millis();
    while (r_millis() < (starttime + 500)) {
        ROB_LOGI("Test", "Work value %i", work_value);
        if (work_value == 999) {       
            TEST_ASSERT_TRUE(true);
            return;
        }
        
        r_delay(10);
    }
    
    TEST_ASSERT_TRUE(false);

}
void tst_queue_shutdown(void) {
    ROB_LOGI("Test", "In tst_queue_shutdown");
    sample_shutdown_worker();
    TEST_ASSERT_TRUE(true);
}

