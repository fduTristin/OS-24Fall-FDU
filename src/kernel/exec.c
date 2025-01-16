#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>

#define Error printk("\033[47;31m(Error)\033[0m")

extern int fdalloc(struct file *f);

int execve(const char *path, char *const argv[], char *const envp[])
{
    /* (Final) TODO BEGIN */
//     char *s, *last;
//     int i, off;
//     u32 argc, sz, sp;
//     Elf64_Ehdr elf;
//     Inode *ip;
//     Elf64_Phdr ph;
//     struct pgdir *pgdir, *oldpgdir;
//     Proc *curproc = thisproc();

//     /**
//      * Step1: Load data from the file stored in `path`.
//      * The first `sizeof(struct Elf64_Ehdr)` bytes is the ELF header part.
//      * You should check the ELF magic number and get the `e_phoff` and `e_phnum` which is the starting byte of program header.
//     */
//     OpContext ctx;
//     bcache.begin_op(&ctx);

//     if ((ip = namei(path, &ctx)) == 0) {
//         bcache.end_op(&ctx);
//         Error;
//         printk("Path %s not found!\n", path);
//         return -1;
//     }

//     inodes.lock(ip);
//     pgdir = NULL;

//     // read elf header & check its length
//     if (inodes.read(ip, (u8 *)(&elf), 0, sizeof(Elf64_Ehdr)) !=
//         sizeof(Elf64_Ehdr)) {
//         Error;
//         printk("Elf header maybe corrupted\n");
//         goto bad;
//     };

//     // check magic number & architecture
//     u8 *e_ident = elf.e_ident;
//     if (strncmp((const char *)e_ident, ELFMAG, SELFMAG) != 0 ||
//         e_ident[EI_CLASS] != ELFCLASS64) {
//         Error;
//         printk("File format not supported.\n");
//         goto bad;
//     }

//     Elf64_Off e_phoff = elf.e_phoff;
//     Elf64_Half e_phnum = elf.e_phnum;

//     pgdir = (struct pgdir *)kalloc(sizeof(struct pgdir));
//     init_pgdir(pgdir);

//     // Load program into memory.
//     sz = 0;
//     for (i = 0, off = e_phoff; i < e_phnum; i++, off += sizeof(ph)) {
//         if (readi(ip, (char *)&ph, off, sizeof(ph)) != sizeof(ph)) {
//             Error;
//             printk("Failed to read program header\n");
//             goto bad;
//         }
//         if (ph.p_type != PT_LOAD)
//             continue;
//         if (ph.memsz < ph.filesz)
//             goto bad;
//         if (ph.vaddr + ph.memsz < ph.vaddr)
//             goto bad;
//         if ((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
//             goto bad;
//         if (ph.vaddr % PGSIZE != 0)
//             goto bad;
//         if (loaduvm(pgdir, (char *)ph.vaddr, ip, ph.off, ph.filesz) < 0)
//             goto bad;
//     }

// bad:
//     if (pgdir) {
//         free_pgdir(pgdir);
//     }
//     if (ip) {
//         inodes.unlockput(&ctx, ip);
//         bcache.end_op(&ctx);
//     }
    return -1;
    /* (Final) TODO END */
}
