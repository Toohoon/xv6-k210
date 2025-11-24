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
  int fd;
  uint64 addr;          // 用户空间 struct kstat* 的地址
  struct file *f;
  struct proc *p = myproc();

  // 取参数：fd, &kst
  if (argint(0, &fd) < 0 || argaddr(1, &addr) < 0)
    return -1;

  // 检查 fd 合法性，并取到 struct file*
  if (fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0)
    return -1;

  // 把状态写到用户空间的 kstat 结构中
  // 由 filestat 负责填各字段（st_dev, st_ino, st_size 等）
  return filestat(f, addr);
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
  int fd[2];
  struct file *rf, *wf;
  struct proc *p = myproc();

  // 创建内核 pipe 对象（rf/wf 是 file* 类型）
  if(pipealloc(&rf, &wf) < 0)
    return -1;  // 分配失败

  // 从用户态取得 fd 数组的地址
  uint64 user_fd_array;
  if(argaddr(0, &user_fd_array) < 0)
    return -1;

  // 分配两个 fd，分别是读端和写端
  int fd0 = -1;
  int fd1 = -1;

  // 为 rf 分配文件描述符
  if((fd0 = fdalloc(rf)) < 0){
    fileclose(rf);
    fileclose(wf);
    return -1;
  }

  // 为 wf 分配文件描述符
  if((fd1 = fdalloc(wf)) < 0){
    p->ofile[fd0] = 0;  // 回滚 fd0
    fileclose(rf);
    fileclose(wf);
    return -1;
  }

  // 写回到用户态的 fd[2]
  fd[0] = fd0;
  fd[1] = fd1;

  // 把 fd[2] 写到用户的地址空间中（必须用 copyout）
  if(copyout(p->pagetable, user_fd_array, (char *)fd, sizeof(fd)) < 0){
    // 如果 copyout 失败，需要回滚
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }

  return 0;   // 成功返回 0
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
struct tms {
  uint64 tms_utime;   // user time in ticks
  uint64 tms_stime;   // sys  time in ticks
  uint64 tms_cutime;  // waited children user time
  uint64 tms_cstime;  // waited children sys  time
};
extern struct spinlock tickslock;
extern uint ticks;
uint64
sys_times(void)
{
  uint64 uaddr;
  if (argaddr(0, &uaddr) < 0) return (uint64)-1;

  struct tms ktms = {0,0,0,0};

  struct proc *p = myproc();  
  if (copyout(p->pagetable, uaddr, (char*)&ktms, sizeof(ktms)) < 0)
    return (uint64)-1;

  acquire(&tickslock);
  uint64 now = ticks;
  release(&tickslock);
  return now;
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
uint64 
sys_gettimeofday(void)
{
  uint64 time;
  uint64 addr;
  if (argaddr(0, &addr) < 0)
    return -1;
  if ((time = r_time()) < 0)
    return -1;
  uint64 sec = time / 10000000;      
  uint64 usec = (time / 10) % 1000000; 
  *(uint64 *)addr = sec;
  *((uint64 *)addr + 1) = usec;
  return 0;
}
uint64
sys_dup3(void)
{
  int oldfd, newfd;
  struct file *f;
  struct proc *p = myproc();

  // 读取两个参数
  if(argint(0, &oldfd) < 0 || argint(1, &newfd) < 0)
    return -1;

  // 检查 oldfd 是否有效，并且取出 struct file*
  if(oldfd < 0 || oldfd >= NOFILE || (f = p->ofile[oldfd]) == 0)
    return -1;

  // 如果 oldfd == newfd，直接返回（dup3规范就是这样）
  if(oldfd == newfd)
    return newfd;

  // newfd 如果已经被使用，先关闭它
  if(newfd < 0 || newfd >= NOFILE)
    return -1;

  if(p->ofile[newfd]){
    fileclose(p->ofile[newfd]);   // 关闭旧的文件
    p->ofile[newfd] = 0;
  }

  // 把 newfd 指向 oldfd 对应的文件结构
  p->ofile[newfd] = f;
  filedup(f);  // 增加引用计数

  return newfd;
}
uint64
sys_getdents64(void)
{
  int fd;
  uint64 ubuf;   // 用户缓冲区地址：注意是 uint64
  int len;       // 用户缓冲区大小
  struct file *f;
  struct proc *p = myproc();

  // 读取参数：fd, buf, len
  if (argint(0, &fd) < 0 || argaddr(1, &ubuf) < 0 || argint(2, &len) < 0)
    return -1;

  // 校验 fd
  if (fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0)
    return -1;

  // 必须是目录类文件（在你这个实现里，一般目录/普通文件都用 FD_ENTRY）
  if (f->type != FD_ENTRY)
    return -1;

  if (len <= 0)
    return -1;

  int nread = 0;   // 已经写入到 buf 的字节数

  while (nread < len) {
    // 记录调用前的偏移量，防止这次 entry 太大放不下，需要回滚
    uint oldoff = f->off;

    // dirnext 会：
    //  - 从当前目录偏移取一条 entry
    //  - 把 struct dirent 写到 ubuf + nread
    //  - 返回写入的字节数（d_reclen）
    //  - 如果到达目录末尾，返回 0
    //  - 如果出错，返回负数
    int r = dirnext(f, ubuf + nread);

    if (r < 0) {
      // 出错：如果啥都没读到，直接返回 -1；否则返回已经读到的字节
      if (nread == 0)
        return -1;
      else
        return nread;
    }

    if (r == 0) {
      // 目录读完：如果这次循环前就没写任何东西，返回 0；否则返回当前累计字节
      break;
    }

    // 这条记录大小是 r，如果加上它会超过缓冲区，就回滚 offset，结束循环
    if (nread + r > len) {
      f->off = oldoff;   // 撤销这次 dirnext 的偏移
      break;
    }

    nread += r;
  }

  // 返回这次真正写入到 buf 的字节数
  // 如果本次一次都没成功写入，而且目录刚好读完，nread == 0 → 返回 0，符合“读到结尾返回 0”的要求
  return nread;
}
uint64
sys_mkdirat(void)
{
  char path[FAT32_MAX_PATH];
  int dirfd, mode;
  struct dirent *ep;

  // 解析参数：dirfd, path, mode
  if (argint(0, &dirfd) < 0 ||
      argstr(1, path, FAT32_MAX_PATH) < 0 ||
      argint(2, &mode) < 0) {
    return -1;
  }

  // 空路径直接失败
  if (strlen(path) == 0)
    return -1;

  // 关键：根据 dirfd + path 计算出绝对路径
  //  - path 是绝对路径：直接保留
  //  - path 是相对路径：
  //      * dirfd == AT_FDCWD → 相对当前工作目录
  //      * 其他 dirfd       → 相对指定目录 fd
  if (get_path(path, dirfd) < 0)
    return -1;

  // 创建目录。create 原型：create(char *path, short type, int mode)
  ep = create(path, T_DIR, mode);
  if (ep == NULL)
    return -1;

  // 解锁并释放 dirent
  eunlock(ep);
  eput(ep);

  return 0;   // 成功
}
uint64
sys_unlinkat(void)
{
  char path[FAT32_MAX_PATH];
  int dirfd;
  int flags;              // 一定要是 int，配合 argint
  struct dirent *ep;
  int is_dir;

  // 解析参数：dirfd, path, flags
  if (argint(0, &dirfd) < 0 ||
      argstr(1, path, FAT32_MAX_PATH) < 0 ||
      argint(2, &flags) < 0) {
    return -1;
  }

  if (strlen(path) == 0)
    return -1;

  // 把相对路径 + dirfd 组合成绝对路径（跟你 sys_mkdirat 一样）
  if (get_path(path, dirfd) < 0)
    return -1;

  // 根据绝对路径找到对应目录项
  ep = ename(path);
  if (ep == NULL)
    return -1;

  elock(ep);

  // 判断是不是目录：看 FAT32 attribute 里的 ATTR_DIRECTORY 位
  is_dir = (ep->attribute & ATTR_DIRECTORY) != 0;

  if (flags & AT_REMOVEDIR) {
    // 要求删除目录，但目标不是目录 → 错
    if (!is_dir) {
      eunlock(ep);
      eput(ep);
      return -1;
    }
  } else {
    // 删除普通文件，但目标是目录 → 错
    if (is_dir) {
      eunlock(ep);
      eput(ep);
      return -1;
    }
  }

  // 统一调用 eremove 删除这个 dirent
  // （目录的空/非空检查、文件数据清理等都在 eremove 里做）
  eremove(ep);

  eunlock(ep);
  eput(ep);

  return 0;
}
