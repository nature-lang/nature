type dir_t = anyptr

pub type dirent_t = struct {
    u64 ino
    u64 off
    u16 reclen
    u16 namlen
    u8 t
    [u8;1024] name
}

#linkid 'opendir$INODE64'
pub fn opendir(anyptr str):dir_t

#linkid 'readdir$INODE64'
pub fn readdir(dir_t d):ptr<dirent_t>

#linkid closedir
pub fn closedir(dir_t d):int