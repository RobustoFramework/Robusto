#include <robusto_time.h>

// TODO: Will this 

rob_ret_val_t r_gettimeofday(struct timeval *tv, struct timezone *tz) {

    return (gettimeofday(tv, tz) == 0) ? ROB_OK:ROB_FAIL;
}

rob_ret_val_t r_settimeofday(const struct timeval *tv, const struct timezone * tz) {
    return (settimeofday(tv, tz) == 0) ? ROB_OK:ROB_FAIL;
    
}
