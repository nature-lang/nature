type dir_t = anyptr

pub type dirent_t = struct {
    u64 ino
    i64 off
    u16 reclen
    u8 t
    [u8;256] name
}

#linkid opendir
pub fn opendir(anyptr str):dir_t

#linkid readdir
pub fn readdir(dir_t d):ptr<dirent_t>

#linkid closedir
pub fn closedir(dir_t d):int