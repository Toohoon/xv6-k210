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
  int flags, omode, fd;
  struct file* f;
  struct dirent* ep;
  if (argstr(0, path, FAT32_MAX_PATH) < 0 || argint(1, &omode) < 0||argint(2, &flags) < 0 )
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
    //if ((ep->attribute & ATTR_DIRECTORY) && omode != O_RDONLY)
    if((ep->attribute & ATTR_DIRECTORY) && (flags & (O_WRONLY | O_RDWR))) 
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
  uint64 hint;     
  int len, prot, flags, fd, off;

  if (argaddr(0, &hint) < 0) return -1;
  if (argint(1, &len)   < 0 || len <= 0) return -1;
  if (argint(2, &prot)  < 0) return -1;
  if (argint(3, &flags) < 0) return -1;
  if (argint(4, &fd)    < 0) return -1;
  if (argint(5, &off)   < 0) return -1;
  if (hint != 0) return -1;
  if (off != 0)  return -1;

  struct proc *p = myproc();

  if (fd < 0 || fd >= NOFILE) return -1;
  struct file *f = p->ofile[fd];
  if (f == 0 || f->type != FD_ENTRY || f->ep == 0) return -1;

  uint64 fsize = (uint64)f->ep->file_size;
  uint64 want  = (uint64)len;
  if (want > fsize) want = fsize;
  if (want == 0) return -1;

  uint64 oldsz   = p->sz;
  uint64 start   = PGROUNDUP(oldsz);
  uint64 map_len = PGROUNDUP(want);

  if (uvmalloc(p->pagetable, p->kpagetable, oldsz, start + map_len) == 0)
    return -1;
  p->sz = start + map_len;

  elock(f->ep);
  int rd = eread(f->ep, 1, start, 0, (int)want);
  eunlock(f->ep);
  if (rd < 0) {
    return -1;
  }
  return start; 
}
uint64 
sys_munmap(void)
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
// static int get_abspath(struct dirent* de, char* path_buf, int buf_size) {
//   if (de == NULL || de->parent == NULL) {
//     if (buf_size < 2) {
//       return -1;
//     }
//     strncpy(path_buf, "/", buf_size);
//     return 0;
//   }
//   if (get_abspath(de->parent, path_buf, buf_size) < 0) {
//     return -1;
//   }
//   int parent_len = strlen(path_buf);
//   if (parent_len > 1) {
//     if (parent_len + 1 >= buf_size) {
//       return -1;
//     }
//     path_buf[parent_len++] = '/';
//     path_buf[parent_len] = '\0';
//   }

//   safestrcpy(path_buf + parent_len, de->filename, buf_size - parent_len);
//   return 0;
// }
// int get_path(char* path, int fd) {
//   if (path == NULL) {
//     return -1;
//   }
//   if (path[0] == '/') {
//     return 0;
//   }
//   if (path[0] == '.' && path[1] == '/') {
//     path += 2;
//   }
//   char base_path[FAT32_MAX_PATH];
//   struct dirent* base_de = NULL;
//   if (fd == AT_FDCWD) {
//     base_de = myproc()->cwd;
//   }
//   else {
//     if (fd < 0 || fd >= NOFILE) {
//       return -1;
//     }
//     struct file* f = myproc()->ofile[fd];
//     if (f == NULL || !(f->ep->attribute & ATTR_DIRECTORY)) {
//       return -1;
//     }
//     base_de = f->ep;
//   }
//   if (get_abspath(base_de, base_path, FAT32_MAX_PATH) < 0) {
//     return -1;
//   }
//   char final_path[FAT32_MAX_PATH];
//   safestrcpy(final_path, base_path, FAT32_MAX_PATH);
//   int base_len = strlen(final_path);
//   if (base_len > 1) {
//     if (base_len + 1 >= sizeof(final_path)) {
//       return -1;
//     }
//     final_path[base_len++] = '/';
//     final_path[base_len] = '\0';
//   }

//   safestrcpy(final_path + base_len, path, FAT32_MAX_PATH - base_len);
//   safestrcpy(path, final_path, FAT32_MAX_PATH);

//   return 0;
// }
// uint64
// sys_openat(void) {
//   char path[FAT32_MAX_PATH];
//   int dirfd, flags, mode, fd;
//   struct file* f;
//   struct dirent* ep;

//   if (
//     argint(0, &dirfd) < 0 ||
//     argstr(1, path, FAT32_MAX_PATH) < 0 ||
//     argint(2, &flags) < 0 ||
//     argint(3, &mode) < 0
//     ) {
//     return -1;
//   }

//   if (strlen(path) == 0) {
//     return -1;
//   }
//   if (get_path(path, dirfd) < 0) {
//     return -1;
//   }
//   if (flags & O_CREATE) {
//     ep = create(path, T_FILE, mode);
//     if (ep == NULL) {
//       return -1;
//     }
//   }
//   else {
//     if ((ep = ename(path)) == NULL) {
//       return -1;
//     }
//     elock(ep);
//     if ((ep->attribute & ATTR_DIRECTORY) && (flags & (O_WRONLY | O_RDWR))) {
//       eunlock(ep);
//       eput(ep);
//       return -1;
//     }
//   }

//   if ((f = filealloc()) == NULL || (fd = fdalloc(f)) < 0) {
//     if (f) {
//       fileclose(f);
//     }
//     eunlock(ep);
//     eput(ep);
//     return -1;
//   }

//   if (!(ep->attribute & ATTR_DIRECTORY) && (flags & O_TRUNC)) {
//     etrunc(ep);
//   }

//   f->type = FD_ENTRY;
//   f->off = (flags & O_APPEND) ? ep->file_size : 0;
//   f->ep = ep;
//   f->readable = !(flags & O_WRONLY);
//   f->writable = (flags & O_WRONLY) || (flags & O_RDWR);

//   eunlock(ep);

//   return fd;
// }

// 1) 안전한 절대경로 구축: 길이 체크 강화
static int get_abspath(struct dirent* de, char* buf, int bufsz) {
  if (de == NULL) return -1;               // de==NULL은 에러로 간주
  if (de->parent == NULL) {                // 루트
    if (bufsz < 2) return -1;
    buf[0] = '/'; buf[1] = '\0';
    return 0;
  }

  if (get_abspath(de->parent, buf, bufsz) < 0) return -1;

  int plen = strlen(buf);
  // 필요하면 슬래시 추가
  if (plen == 1 && buf[0] == '/') {
    // "/name"
  } else {
    if (plen + 1 >= bufsz) return -1;
    buf[plen++] = '/';
    buf[plen] = '\0';
  }

  int nlen = strlen(de->filename);
  if (plen + nlen + 1 > bufsz) return -1;  // +1 for '\0'
  memmove(buf + plen, de->filename, nlen + 1);
  return 0;
}

// 2) 입력 포인터를 절대 이동시키지 않기
static int get_path(char* out_path, int dirfd) {
  if (out_path == NULL) return -1;

  // 절대경로면 그대로
  if (out_path[0] == '/') return 0;

  const char* rel = out_path;
  if (rel[0] == '.' && rel[1] == '/')
    rel += 2;  // 로컬 포인터만 이동 (out_path는 건들지 않음)

  struct dirent* base_de = NULL;
  if (dirfd == AT_FDCWD) {
    base_de = myproc()->cwd;
  } else {
    if (dirfd < 0 || dirfd >= NOFILE) return -1;
    struct file* f = myproc()->ofile[dirfd];
    if (f == NULL || f->ep == NULL) return -1;
    if (!(f->ep->attribute & ATTR_DIRECTORY)) return -1;
    base_de = f->ep;
  }

  char base[FAT32_MAX_PATH];
  if (get_abspath(base_de, base, FAT32_MAX_PATH) < 0) return -1;

  // 길이 확인 후 결합
  int blen = strlen(base);
  int rlen = strlen(rel);
  int need = blen + (blen > 1 ? 1 : 0) + rlen + 1; // '/' + rel + '\0'
  if (need > FAT32_MAX_PATH) return -1;

  char final[FAT32_MAX_PATH];
  safestrcpy(final, base, FAT32_MAX_PATH);
  if (blen > 1) {
    final[blen++] = '/';
    final[blen] = '\0';
  }
  safestrcpy(final + blen, rel, FAT32_MAX_PATH - blen);

  // out_path의 원래 시작주소에 결과 복사
  safestrcpy(out_path, final, FAT32_MAX_PATH);
  return 0;
}

uint64
sys_openat(void) {
  char path[FAT32_MAX_PATH];
  int dirfd, flags, mode, fd;
  struct file* f = NULL;
  struct dirent* ep = NULL;

  if (argint(0, &dirfd) < 0 ||
      argstr(1, path, FAT32_MAX_PATH) < 0 ||
      argint(2, &flags) < 0 ||
      argint(3, &mode) < 0) {
    return -1;
  }

  if (path[0] == '\0') return -1;
  if (get_path(path, dirfd) < 0) return -1;

  if (flags & O_CREATE) {
    // 선택: O_EXCL 지원시, 존재하면 실패 처리
    // if (flags & O_EXCL) { if (ename(path) != NULL) return -1; }
    ep = create(path, T_FILE, mode);
    if (ep == NULL) return -1; // create는 ep를 락 잡은 상태로 반환한다고 가정
  } else {
    ep = ename(path);
    if (ep == NULL) return -1;
    elock(ep);
    if ((ep->attribute & ATTR_DIRECTORY) && (flags & (O_WRONLY | O_RDWR))) {
      eunlock(ep);
      eput(ep);
      return -1;
    }
  }

  // 파일/FD 확보
  f = filealloc();
  if (f == NULL) {
    eunlock(ep);
    eput(ep);
    return -1;
  }
  fd = fdalloc(f);
  if (fd < 0) {
    fileclose(f);
    eunlock(ep);
    eput(ep);
    return -1;
  }

  // O_TRUNC는 쓰기 모드일 때만
  if (!(ep->attribute & ATTR_DIRECTORY) &&
      (flags & O_TRUNC) &&
      (flags & (O_WRONLY | O_RDWR))) {
    etrunc(ep);
  }

  f->type = FD_ENTRY;
  f->ep = ep;
  f->off = (flags & O_APPEND) ? ep->file_size : 0;
  f->readable = !(flags & O_WRONLY);
  f->writable = (flags & (O_WRONLY | O_RDWR)) != 0;

  eunlock(ep);
  return fd;
}
