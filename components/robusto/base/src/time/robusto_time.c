#include <robusto_time.h>

#if defined(_WIN32)
#include <errno.h>
#endif

// TODO: Will this 

rob_ret_val_t r_gettimeofday(struct timeval *tv, struct timezone *tz) {

    return (gettimeofday(tv, tz) == 0) ? ROB_OK:ROB_FAIL;
}

rob_ret_val_t r_settimeofday(const struct timeval *tv, const struct timezone * tz) {
#if defined(_WIN32)
    (void)tv;
    (void)tz;
    errno = ENOSYS;
    return ROB_FAIL;
#else
    return (settimeofday(tv, tz) == 0) ? ROB_OK:ROB_FAIL;
#endif
}
