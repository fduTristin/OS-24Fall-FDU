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

#define USER_STACK_TOP 0x80000000
#define USER_STACK_SIZE 0x8000 // 8M
#define RESERVE_SIZE 0x40
#define Error printk("\033[47;31m(Error)\033[0m")

extern int fdalloc(struct file *f);

int execve(const char *path, char *const argv[], char *const envp[])
{
    /* (Final) TODO BEGIN */
    Elf64_Ehdr elf;
    Inode *ip;
    Elf64_Phdr phdr;
    struct pgdir *pgdir, *oldpgdir;
    Proc *curproc = thisproc();

    /*
     * Step1: Load data from the file stored in `path`.
     * The first `sizeof(struct Elf64_Ehdr)` bytes is the ELF header part.
     * You should check the ELF magic number and get the `e_phoff` and `e_phnum` which is the starting byte of program header.
     *
     */

    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = namei(path, &ctx)) == 0) {
        bcache.end_op(&ctx);
        Error;
        // printk("Path %s not found!\n", path);
        return -1;
    }

    // printk("Path %s found!\n", path);

    inodes.lock(ip);
    pgdir = NULL;

    // read elf header & check its length
    if (inodes.read(ip, (u8 *)(&elf), 0, sizeof(Elf64_Ehdr)) !=
        sizeof(Elf64_Ehdr)) {
        Error;
        // printk("Elf header maybe corrupted\n");
        goto bad;
    };

    // check magic number & architecture
    u8 *e_ident = elf.e_ident;
    if (strncmp((const char *)e_ident, ELFMAG, SELFMAG) != 0 ||
        e_ident[EI_CLASS] != ELFCLASS64) {
        Error;
        // printk("File format not supported.\n");
        goto bad;
    }

    // printk("Proper Format\n");

    Elf64_Off e_phoff = elf.e_phoff;
    Elf64_Half e_phnum = elf.e_phnum;

    pgdir = (struct pgdir *)kalloc(sizeof(struct pgdir));
    init_pgdir(pgdir);

    /*
     * Step2: Load program headers and the program itself
     * Program headers are stored like: struct Elf64_Phdr phdr[e_phnum];
     * e_phoff is the offset of the headers in file, namely, the address of phdr[0].
     * For each program header, if the type(p_type) is LOAD, you should load them:
     * A naive way is 
     * (1) allocate memory, va region [vaddr, vaddr+filesz)
     * (2) copy [offset, offset + filesz) of file to va [vaddr, vaddr+filesz) of memory
     * Since we have applied dynamic virtual memory management, you can try to only set the file and offset (lazy allocation)
     * (hints: there are two loadable program headers in most exectuable file at this lab, 
     * the first header indicates the text section(flag=RX) and the second one is the data+bss section(flag=RW). 
     * You can verify that by check the header flags. 
     * The second header has [p_vaddr, p_vaddr+p_filesz) the data section and [p_vaddr+p_filesz, p_vaddr+p_memsz) the bss section which is required to set to 0, 
     * you may have to put data and bss in a single struct section. COW by using the zero page is encouraged)
     */

    u64 section_top = 0;
    for (u64 i = 0, off = e_phoff; i < e_phnum; i++, off += sizeof(phdr)) {
        // load program headers
        if (inodes.read(ip, (u8 *)&phdr, off, sizeof(phdr)) != sizeof(phdr)) {
            Error;
            // printk("Failed to read program header\n");
            goto bad;
        }
        if (phdr.p_type != PT_LOAD)
            continue;

        section_top = MAX(section_top, phdr.p_vaddr + phdr.p_memsz);

        // section
        struct section *sec = (struct section *)kalloc(sizeof(struct section));
        init_section(sec);
        sec->begin = phdr.p_vaddr;

        Elf64_Word p_flags = phdr.p_flags;
        if (p_flags == (PF_R | PF_X)) {
            // text
            sec->flags = ST_TEXT;
            sec->end = sec->begin + phdr.p_filesz;

            // lazy allocation
            sec->fp = file_alloc();
            sec->fp->ip = inodes.share(ip);
            sec->fp->readable = TRUE;
            sec->fp->writable = FALSE;
            sec->fp->ref = 1;
            sec->fp->off = 0;
            sec->fp->type = FD_INODE;
            sec->length = phdr.p_filesz;
            sec->offset = phdr.p_offset;
        } else if (p_flags == (PF_R | PF_W)) {
            // data & bss
            // printk("init data section\n");
            sec->flags = ST_DATA;
            sec->end = sec->begin + phdr.p_memsz;

            // [p_vaddr, p_vaddr + p_file_sz) is data section
            // [p_vaddr + p_file_sz, p_vaddr + p_mem_sz) is bss section
            // fill bss section with 0

            u64 filesz = phdr.p_filesz, offset = phdr.p_offset,
                va = phdr.p_vaddr;

            // load data section
            while (filesz) {
                u64 cursize = MIN(filesz, (u64)PAGE_SIZE - VA_OFFSET(va));
                // printk("va: %llx, cursize: %lld, filesize: %lld\n", va, cursize,
                //    filesz);
                void *p = kalloc_page();
                memset(p, 0, PAGE_SIZE);
                vmmap(pgdir, PAGE_BASE(va), p, PTE_USER_DATA | PTE_RW);
                if (inodes.read(ip, (u8 *)(p + VA_OFFSET(va)), offset,
                                cursize) != cursize) {
                    Error;
                    // printk("Failed to read data section\n");
                    goto bad;
                }
                filesz -= cursize;
                offset += cursize;
                va += cursize;
            }
            // if(0x404008 <sec->end && 0x404008 >= sec->begin){
            //    printk("0x404008: %llx\n", *((u64 *)0x404008));
            // }
            // printk("finish data section loading\n");
            ASSERT(va == phdr.p_vaddr + phdr.p_filesz);
            // Now consider size of the bss section
            // If it ends at the last page of data section, do nothing (since the rest of last page is already set to 0)
            // Otherwise it's necessary to map zeropage to the pgdir of bss section's address
            if (PAGE_BASE(va) + PAGE_SIZE < phdr.p_vaddr + phdr.p_memsz) {
                va = PAGE_BASE(va) + PAGE_SIZE;
                filesz = phdr.p_vaddr + phdr.p_memsz -
                         va; // size of the rest of bss section
                while (filesz > 0) {
                    u64 cursize = MIN((u64)PAGE_SIZE, filesz);
                    vmmap(pgdir, PAGE_BASE(va), get_zero_page(),
                          PTE_USER_DATA | PTE_RO);
                    filesz -= cursize;
                    va += cursize;
                }
                ASSERT(filesz == 0);
                ASSERT(va == phdr.p_vaddr + phdr.p_memsz);
            }
        } else {
            Error;
            // printk("Invalid program header type\n");
            goto bad;
        }

        _insert_into_list(&pgdir->section_head, &sec->stnode);
    }

    inodes.unlockput(&ctx, ip);
    bcache.end_op(&ctx);

    // init the heap section
    struct section *heap = (struct section *)kalloc(sizeof(struct section));
    memset(heap, 0, sizeof(struct section));
    heap->begin = heap->end = PAGE_BASE(section_top) + PAGE_SIZE;
    heap->flags = ST_HEAP;
    _insert_into_list(&pgdir->section_head, &heap->stnode);

    /*
    * Step3: Allocate and initialize user stack.
    * The va of the user stack is not required to be any fixed value. It can be randomized. (hints: you can directly allocate user stack at one time, or apply lazy allocation)
    * Push argument strings.
    * The initial stack like:
    *   +-------------+
    *   | envp[m][sm] |
    *   +-------------+
    *   |    ....     |
    *   +-------------+
    *   | envp[m][0]  |
    *   +-------------+
    *   |    ....     |
    *   +-------------+
    *   | envp[0][s0] |
    *   +-------------+
    *   |    ....     |
    *   +-------------+
    *   | envp[0][0]  | 
    *   +-------------+
    *   | argv[n][sn] |
    *   +-------------+
    *   |    ....     |
    *   +-------------+
    *   | argv[n][0]  |
    *   +-------------+
    *   |    ....     |
    *   +-------------+
    *   | argv[0][s0] |
    *   +-------------+
    *   |    ....     |
    *   +-------------+
    *   | argv[0][0]  |  
    *   +-------------+  <== str_start
    *   | envp_ptr[m] |  = 0
    *   +-------------+
    *   |    ....     |
    *   +-------------+  
    *   | envp_ptr[0] |
    *   +-------------+  <== envp_start
    *   | argv_ptr[n] |  = 0
    *   +-------------+
    *   |    ....     |
    *   +-------------+  
    *   | argv_ptr[0] |
    *   +-------------+  <== argv_start
    *   |    argc     |
    *   +-------------+  <== sp

    * ## Example
    * sp -= 8; *(size_t *)sp = argc; (hints: sp can be directly written if current pgdir is the new one)
    * thisproc()->tf->sp = sp; (hints: Stack pointer must be aligned to 16B!)
    * The entry point addresses is stored in elf_header.entry
    */

    // lazy allocation
    // void *p = kalloc_page();
    // memset(p, 0, PAGE_SIZE);
    // vmmap(pgdir, USER_STACK_TOP - PAGE_SIZE, p, PTE_USER_DATA | PTE_RW);
    // printk("begin initialize stack\n");
    u64 top = USER_STACK_TOP - RESERVE_SIZE;
    struct section *st_ustack =
            (struct section *)kalloc(sizeof(struct section));
    memset(st_ustack, 0, sizeof(struct section));
    st_ustack->begin = USER_STACK_TOP - USER_STACK_SIZE;
    st_ustack->end = USER_STACK_TOP;
    st_ustack->flags = ST_USER_STACK;
    init_list_node(&st_ustack->stnode);
    _insert_into_list(&pgdir->section_head, &st_ustack->stnode);

    // fill in the args
    u64 argc = 0, arg_len = 0, envc = 0, env_len = 0, zero = 0;
    if (envp) {
        // printk("envp[%lld]: ", envc);
        // printk("%p\n", envp[envc]);
        while (envp[envc]) {
            // printk("envp[%lld]: ", envc);
            // printk("%p\n", envp[envc]);
            env_len += strlen(envp[envc]) + 1;
            envc++;
        }
    }
    if (argv) {
        while (argv[argc]) {
            arg_len += strlen(argv[argc]) + 1;
            argc++;
        }
    }

    u64 str_total_len = env_len + arg_len;
    u64 str_start = top - str_total_len;
    u64 ptr_tot = (2 + argc + envc + 1) * 8;
    u64 argc_start = (str_start - ptr_tot) & (~0xf);
    if (argc_start < USER_STACK_TOP - USER_STACK_SIZE) {
        // printk("Too many varibles");
        PANIC();
    }
    u64 argv_start = argc_start + 8;
    u64 sp = argc_start;
    copyout(pgdir, (void *)sp, &argc, 8);

    // copy strings of argv & envp and argv & envp
    for (u64 i = 0; i < argc; i++) {
        usize len = strlen(argv[i]) + 1;
        copyout(pgdir, (void *)str_start, argv[i], len);
        copyout(pgdir, (void *)argv_start, &str_start, 8);
        str_start += len;
        argv_start += 8;
    }
    copyout(pgdir, (void *)argv_start, &zero, 8); // argv[n] = 0

    argv_start += 8;
    for (u64 i = 0; i < envc; i++) {
        usize len = strlen(envp[i]) + 1;
        copyout(pgdir, (void *)str_start, envp[i], len);
        copyout(pgdir, (void *)argv_start, &str_start, 8);
        str_start += len;
        argv_start += 8;
    }
    copyout(pgdir, (void *)argv_start, &zero, 8); // envp[m] = 0

    curproc->ucontext->sp = sp;

    oldpgdir = &curproc->pgdir;
    free_pgdir(oldpgdir);
    curproc->ucontext->elr = elf.e_entry;
    memcpy(&curproc->pgdir, pgdir, sizeof(struct pgdir));
    init_list_node(&curproc->pgdir.section_head);
    _insert_into_list(&pgdir->section_head, &curproc->pgdir.section_head);
    _detach_from_list(&pgdir->section_head);
    kfree(pgdir);
    attach_pgdir(&curproc->pgdir);
    return 0;

bad:
    if (pgdir) {
        free_pgdir(pgdir);
    }
    if (ip) {
        inodes.unlockput(&ctx, ip);
        bcache.end_op(&ctx);
    }
    return -1;
    /* (Final) TODO END */
}
