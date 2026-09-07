#include <sys/stat.h>
#include <sys/times.h>
#include <errno.h>
#include <stdint.h>

extern "C" {

int _open(const char*, int, int) {
    errno = ENOSYS;
    return -1;
}

clock_t _times(struct tms* buf) {
    if (buf) {
        buf->tms_utime = 0;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return 0;
}

}
