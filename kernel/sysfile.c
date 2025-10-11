//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//


#include "include/types.h"
#include "include/riscv.h"
#include "include/param.h"
#include "include/stat.h"
#include "include/spinlock.h"
#include "include/proc.h"
#include "include/sleeplock.h"
#include "include/file.h"
#include "include/pipe.h"
#include "include/fcntl.h"
#include "include/fat32.h"
#include "include/syscall.h"
#include "include/string.h"
#include "include/printf.h"
#include "include/vm.h"
//#include "include/defs.h"


// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == NULL)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

struct kstat
{
  uint64 st_dev;
  uint64 st_ino;
  unsigned int st_mode;
  uint32 st_nlink;
  uint32 st_uid;
  uint32 st_gid;
  uint64 st_rdev;
  unsigned long __pad;
  long int st_size;
  uint32 st_blksize;
  int __pad2;
  uint64 st_blocks;
  long st_atime_sec;
  long st_atime_nsec;
  long st_mtime_sec;
  long st_mtime_nsec;
  long st_ctime_sec;
  long st_ctime_nsec;
  unsigned __unused[2];
};

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

static struct dirent*
create(char *path, short type, int mode)
{
  struct dirent *ep, *dp;
  char name[FAT32_MAX_FILENAME + 1];

  if((dp = enameparent(path, name)) == NULL)
    return NULL;

  if (type == T_DIR) {
    mode = ATTR_DIRECTORY;
  } else if (mode & O_RDONLY) {
    mode = ATTR_READ_ONLY;
  } else {
    mode = 0;  
  }

  elock(dp);
  if ((ep = ealloc(dp, name, mode)) == NULL) {
    eunlock(dp);
    eput(dp);
    return NULL;
  }
  
  if ((type == T_DIR && !(ep->attribute & ATTR_DIRECTORY)) ||
      (type == T_FILE && (ep->attribute & ATTR_DIRECTORY))) {
    eunlock(dp);
    eput(ep);
    eput(dp);
    return NULL;
  }

  eunlock(dp);
  eput(dp);

  elock(ep);
  return ep;
}

uint64
sys_open(void)
{
  char path[FAT32_MAX_PATH];
  int fd, omode;
  struct file *f;
  struct dirent *ep;

  if (argstr(0, path, FAT32_MAX_PATH) < 0 || argint(1, &omode) < 0)
    return -1;

  if (omode & O_CREATE)
  {
    ep = create(path, T_FILE, omode);
    if (ep == NULL)
    {
      return -1;
    }
  }
  else
  {
    if ((ep = ename(path)) == NULL)
    {
      return -1;
    }
    elock(ep);
    if ((ep->attribute & ATTR_DIRECTORY) && omode != O_RDONLY)
    {
      eunlock(ep);
      eput(ep);
      return -1;
    }
  }

  if ((f = filealloc()) == NULL || (fd = fdalloc(f)) < 0)
  {
    if (f)
    {
      fileclose(f);
    }
    eunlock(ep);
    eput(ep);
    return -1;
  }

  if (!(ep->attribute & ATTR_DIRECTORY) && (omode & O_TRUNC))
  {
    etrunc(ep);
  }

  f->type = FD_ENTRY;
  f->off = (omode & O_APPEND) ? ep->file_size : 0;
  f->ep = ep;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  eunlock(ep);

  return fd;
}
static int get_abspath(struct dirent* de, char* path_buf, int buf_size) {
  if (de == NULL || de->parent == NULL) {
    if (buf_size < 2) {
      return -1;
    }
    strncpy(path_buf, "/", buf_size);
    return 0;
  }
  if (get_abspath(de->parent, path_buf, buf_size) < 0) {
    return -1;
  }
  int parent_len = strlen(path_buf);
  if (parent_len > 1) {
    if (parent_len + 1 >= buf_size) {
      return -1;
    }
    path_buf[parent_len++] = '/';
    path_buf[parent_len] = '\0';
  }

  safestrcpy(path_buf + parent_len, de->filename, buf_size - parent_len);
  return 0;
}
int get_path(char* path, int fd) {
  if (path == NULL) {
    return -1;
  }
  if (path[0] == '/') {
    return 0;
  }
  if (path[0] == '.' && path[1] == '/') {
    path += 2;
  }
  char base_path[FAT32_MAX_PATH];
  struct dirent* base_de = NULL;
  if (fd == AT_FDCWD) {
    base_de = myproc()->cwd;
  }
  else {
    if (fd < 0 || fd >= NOFILE) {
      return -1;
    }
    struct file* f = myproc()->ofile[fd];
    if (f == NULL || !(f->ep->attribute & ATTR_DIRECTORY)) {
      return -1;
    }
    base_de = f->ep;
  }
  if (get_abspath(base_de, base_path, FAT32_MAX_PATH) < 0) {
    return -1;
  }
  char final_path[FAT32_MAX_PATH];
  safestrcpy(final_path, base_path, FAT32_MAX_PATH);
  int base_len = strlen(final_path);
  if (base_len > 1) {
    if (base_len + 1 >= sizeof(final_path)) {
      return -1;
    }
    final_path[base_len++] = '/';
    final_path[base_len] = '\0';
  }

  safestrcpy(final_path + base_len, path, FAT32_MAX_PATH - base_len);
  safestrcpy(path, final_path, FAT32_MAX_PATH);

  return 0;
}
uint64
sys_openat(void) {
  char path[FAT32_MAX_PATH];
  int dirfd, flags, mode, fd;
  struct file* f;
  struct dirent* ep;

  if (
    argint(0, &dirfd) < 0 ||
    argstr(1, path, FAT32_MAX_PATH) < 0 ||
    argint(2, &flags) < 0 ||
    argint(3, &mode) < 0
    ) {
    return -1;
  }

  if (strlen(path) == 0) {
    return -1;
  }
  if (get_path(path, dirfd) < 0) {
    return -1;
  }
  if (flags & O_CREATE) {
    ep = create(path, T_FILE, mode);
    if (ep == NULL) {
      return -1;
    }
  }
  else {
    if ((ep = ename(path)) == NULL) {
      return -1;
    }
    elock(ep);
    if ((ep->attribute & ATTR_DIRECTORY) && (flags & (O_WRONLY | O_RDWR))) {
      eunlock(ep);
      eput(ep);
      return -1;
    }
  }

  if ((f = filealloc()) == NULL || (fd = fdalloc(f)) < 0) {
    if (f) {
      fileclose(f);
    }
    eunlock(ep);
    eput(ep);
    return -1;
  }

  if (!(ep->attribute & ATTR_DIRECTORY) && (flags & O_TRUNC)) {
    etrunc(ep);
  }

  f->type = FD_ENTRY;
  f->off = (flags & O_APPEND) ? ep->file_size : 0;
  f->ep = ep;
  f->readable = !(flags & O_WRONLY);
  f->writable = (flags & O_WRONLY) || (flags & O_RDWR);

  eunlock(ep);

  return fd;
}
uint64 sys_munmap(void)
{
  uint64 addr;
  int len;
  if (argaddr(0, &addr) < 0 || argint(1, &len) < 0 || len <= 0)
  {
    return -1;
  }
  if (addr % PGSIZE != 0)
  {
    return -1;
  }
  struct proc *p = myproc();
  int npages = len / PGSIZE;
  if (addr + (uint64)len > p->sz)
  {
    return -1;
  }
  vmunmap(p->pagetable, addr, npages, 0);
  return 0;
}
uint64
sys_mkdir(void)
{
  char path[FAT32_MAX_PATH];
  struct dirent *ep;

  if(argstr(0, path, FAT32_MAX_PATH) < 0 || (ep = create(path, T_DIR, 0)) == 0){
    return -1;
  }
  eunlock(ep);
  eput(ep);
  return 0;
}

uint64
sys_chdir(void)
{
  char path[FAT32_MAX_PATH];
  struct dirent *ep;
  struct proc *p = myproc();
  
  if(argstr(0, path, FAT32_MAX_PATH) < 0 || (ep = ename(path)) == NULL){
    return -1;
  }
  elock(ep);
  if(!(ep->attribute & ATTR_DIRECTORY)){
    eunlock(ep);
    eput(ep);
    return -1;
  }
  eunlock(ep);
  eput(p->cwd);
  p->cwd = ep;
  return 0;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  // if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
  //    copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
  if(copyout2(fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout2(fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// To open console device.
uint64
sys_dev(void)
{
  int fd, omode;
  int major, minor;
  struct file *f;

  if(argint(0, &omode) < 0 || argint(1, &major) < 0 || argint(2, &minor) < 0){
    return -1;
  }

  if(omode & O_CREATE){
    panic("dev file on FAT");
  }

  if(major < 0 || major >= NDEV)
    return -1;

  if((f = filealloc()) == NULL || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    return -1;
  }

  f->type = FD_DEVICE;
  f->off = 0;
  f->ep = 0;
  f->major = major;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  return fd;
}

// To support ls command
uint64
sys_readdir(void)
{
  struct file *f;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argaddr(1, &p) < 0)
    return -1;
  return dirnext(f, p);
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct dirent *dp)
{
  struct dirent ep;
  int count;
  int ret;
  ep.valid = 0;
  ret = enext(dp, &ep, 2 * 32, &count);   // skip the "." and ".."
  return ret == -1;
}

uint64
sys_remove(void)
{
  char path[FAT32_MAX_PATH];
  struct dirent *ep;
  int len;
  if((len = argstr(0, path, FAT32_MAX_PATH)) <= 0)
    return -1;

  char *s = path + len - 1;
  while (s >= path && *s == '/') {
    s--;
  }
  if (s >= path && *s == '.' && (s == path || *--s == '/')) {
    return -1;
  }
  
  if((ep = ename(path)) == NULL){
    return -1;
  }
  elock(ep);
  if((ep->attribute & ATTR_DIRECTORY) && !isdirempty(ep)){
      eunlock(ep);
      eput(ep);
      return -1;
  }
  elock(ep->parent);      // Will this lead to deadlock?
  eremove(ep);
  eunlock(ep->parent);
  eunlock(ep);
  eput(ep);

  return 0;
}

// Must hold too many locks at a time! It's possible to raise a deadlock.
// Because this op takes some steps, we can't promise
uint64
sys_rename(void)
{
  char old[FAT32_MAX_PATH], new[FAT32_MAX_PATH];
  if (argstr(0, old, FAT32_MAX_PATH) < 0 || argstr(1, new, FAT32_MAX_PATH) < 0) {
      return -1;
  }

  struct dirent *src = NULL, *dst = NULL, *pdst = NULL;
  int srclock = 0;
  char *name;
  if ((src = ename(old)) == NULL || (pdst = enameparent(new, old)) == NULL
      || (name = formatname(old)) == NULL) {
    goto fail;          // src doesn't exist || dst parent doesn't exist || illegal new name
  }
  for (struct dirent *ep = pdst; ep != NULL; ep = ep->parent) {
    if (ep == src) {    // In what universe can we move a directory into its child?
      goto fail;
    }
  }

  uint off;
  elock(src);     // must hold child's lock before acquiring parent's, because we do so in other similar cases
  srclock = 1;
  elock(pdst);
  dst = dirlookup(pdst, name, &off);
  if (dst != NULL) {
    eunlock(pdst);
    if (src == dst) {
      goto fail;
    } else if (src->attribute & dst->attribute & ATTR_DIRECTORY) {
      elock(dst);
      if (!isdirempty(dst)) {    // it's ok to overwrite an empty dir
        eunlock(dst);
        goto fail;
      }
      elock(pdst);
    } else {                    // src is not a dir || dst exists and is not an dir
      goto fail;
    }
  }

  if (dst) {
    eremove(dst);
    eunlock(dst);
  }
  memmove(src->filename, name, FAT32_MAX_FILENAME);
  emake(pdst, src, off);
  if (src->parent != pdst) {
    eunlock(pdst);
    elock(src->parent);
  }
  eremove(src);
  eunlock(src->parent);
  struct dirent *psrc = src->parent;  // src must not be root, or it won't pass the for-loop test
  src->parent = edup(pdst);
  src->off = off;
  src->valid = 1;
  eunlock(src);

  eput(psrc);
  if (dst) {
    eput(dst);
  }
  eput(pdst);
  eput(src);

  return 0;

fail:
  if (srclock)
    eunlock(src);
  if (dst)
    eput(dst);
  if (pdst)
    eput(pdst);
  if (src)
    eput(src);
  return -1;
}
// kernel/sysfile.c 或你现在放 sys_getcwd 的文件里
uint64
sys_getcwd(void)
{
  uint64 addr;
  int size;

  // 取两个参数：buf 地址 + buf 大小
  if (argaddr(0, &addr) < 0 || argint(1, &size) < 0)
    return 0;                       

  if (addr == 0 || size <= 0)
    return 0;

  struct dirent *de = myproc()->cwd;
  char path[FAT32_MAX_PATH];
  char *s;
  int len;

  if (de->parent == NULL) {
    s = "/";
  } else {
    s = path + FAT32_MAX_PATH - 1;
    *s = '\0';
    while (de->parent) {
      len = strlen(de->filename);
      s -= len;
      if (s <= path)          // can't reach root "/"
        return -1;
      strncpy(s, de->filename, len);
      *--s = '/';
      de = de->parent;
    }
  }

  int n = (int)strlen(s) + 1;       
  if (n > size)                     
    return 0;

  if (copyout2(addr, s, n) < 0)
    return 0;

  return addr;
}

uint64
sys_mmap(void)
{
  uint64 hint;      // 允许为 0 (NULL)
  int len, prot, flags, fd, off;

  if (argaddr(0, &hint) < 0) return -1;
  if (argint(1, &len)   < 0 || len <= 0) return -1;
  if (argint(2, &prot)  < 0) return -1;
  if (argint(3, &flags) < 0) return -1;
  if (argint(4, &fd)    < 0) return -1;
  if (argint(5, &off)   < 0) return -1;

  // 简化语义：只接受 hint==0 且 off==0
  if (hint != 0) return -1;
  if (off != 0)  return -1;

  struct proc *p = myproc();

  if (fd < 0 || fd >= NOFILE) return -1;
  struct file *f = p->ofile[fd];
  if (f == 0 || f->type != FD_ENTRY || f->ep == 0) return -1;

  // 计算需要映射的长度：不超过文件大小
  uint64 fsize = (uint64)f->ep->file_size;
  uint64 want  = (uint64)len;
  if (want > fsize) want = fsize;
  if (want == 0) return -1;

  // 从进程末尾页对齐分配
  uint64 oldsz   = p->sz;
  uint64 start   = PGROUNDUP(oldsz);
  uint64 map_len = PGROUNDUP(want);

  if (uvmalloc(p->pagetable, p->kpagetable, oldsz, start + map_len) == 0)
    return -1;
  p->sz = start + map_len;

  // 文件内容 -> 用户地址空间
  elock(f->ep);
  int rd = eread(f->ep, /*user=*/1, start, /*off=*/0, (int)want);
  eunlock(f->ep);
  if (rd < 0) {
    // 这里最简单处理：读失败直接返回 -1（不做回滚）
    // 如果你想严谨点，可在此处 uvmunmap 回滚到 oldsz
    return -1;
  }

  // 剩余尾页（如果 want 不是页整倍数）保持 0 即可
  // 不记录 last_map_*，不做写回

  return start; // 返回用户虚拟地址
}
