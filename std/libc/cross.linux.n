#linkid timegm
pub fn timegm(anyptr value):i64

#linkid atol
pub fn atol(anyptr value):i64

#linkid strtol
pub fn strtol(anyptr value, anyptr endptr, i32 base):i64

#linkid strtoul
pub fn strtoul(anyptr value, anyptr endptr, i32 base):u64

#linkid localtime
pub fn localtime(anyptr timestamp):anyptr

#linkid gmtime
pub fn gmtime(anyptr timestamp):anyptr
