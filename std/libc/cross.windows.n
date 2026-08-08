#linkid _mkgmtime64
pub fn timegm(anyptr value):i64

#linkid _atoi64
pub fn atol(anyptr value):i64

#linkid _strtoi64
pub fn strtol(anyptr value, anyptr endptr, i32 base):i64

#linkid _strtoui64
pub fn strtoul(anyptr value, anyptr endptr, i32 base):u64

#linkid rt_windows_localtime
pub fn localtime(anyptr timestamp):anyptr

#linkid rt_windows_gmtime
pub fn gmtime(anyptr timestamp):anyptr
