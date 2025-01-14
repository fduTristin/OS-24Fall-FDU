# Final

## 1. File Descriptor

### 1.1 任务

> - [x] 任务1：实现`src/fs/file.c`的下列函数：
>
>   ```c
>   // 从全局文件表中分配一个空闲的文件
>   struct file* file_alloc();
>   
>   // 获取文件的元信息（类型、偏移量等）
>   int file_stat(struct file* f, struct stat* st);
>   
>   // 关闭文件
>   void file_close(struct file* f);
>   
>   // 将长度为 n 的 addr 写入 f 的当前偏移处
>   isize file_read(struct file* f, char* addr, isize n);
>   
>   // 将长度为 n 的 addr 从 f 的当前偏移处读入
>   isize file_write(struct file* f, char* addr, isize n);
>   
>   // 文件的引用数+1
>   struct file* file_dup(struct file* f);
>   
>   // 初始化全局文件表
>   void init_ftable();
>   
>   // 初始化/释放进程文件表
>   void init_oftable(struct oftable*);
>   void free_oftable(struct oftable*);
>   ```
>
> - `file_alloc`
>
>   - 遍历`ftable`，找到`ref == 0`的`file`，将`ref`设为`1`，返回对应的`file`
>   - 为了方便调试，这里添加了失败检查，如果未找到合适的`file`，`PANIC()`
>
> - `file_stat`
>
>   - 直接用`stati`就好了
>
> - `file_close`
>
>   - `ref`减一
>   - `f->type == FD_PIPE`: `pipe_close`，这是后话
>   - `f->type == FD_INODE`: 需要进行`inode_put`
>
> - `file_read`
>
>   - 还是根据`f->type`分类
>   - `FD_PIPE`: `pipe_read`（这也是后话）
>   - `FD_INODE`: 调用`inode_read`
>
> - `file_write`
>
>   - 和`read`类似，不过需要注意的是，一次`write`不一定能够在一次事务完成，因为有`OP_MAX_NUM_BLOCKS`的限制，因此我使用多个事务，每个事务的写数据量略少于`OP_MAX_NUM_BLOCKS`
>
>     > [!note]
>     >
>     > （我认为这是更不容易出问题的）
>     >
>     > - [ ] 在后面会测试和优化
>
>   - 此外要保证写的范围不能超出`INODE_MAX_BYTES`
>
> - `file_dup`
>
>   - 很简单的逻辑，`ref`加一
>
> - `init_ftable`
>
>   - 初始化锁
>
> - `init_oftable`
>
>   - `memset`一下
>
> - `free_oftable`
>
>   - 暂时不知道有什么用，之后再说

---

> - [ ] 任务2：编写路径字符串（例如 `/this/is//my/path/so_cool.txt`）的解析逻辑。助教已经在`src/fs/inode.c`中实现了比较困难的部分，需要我完成其中的`namex`函数（可见`inode.c`中的注释部分）：
>
>   ```c
>   static Inode* namex(const char* path, bool nameiparent, char* name, OpContext* ctx)
>   // 给出path对应的inode
>   // 如果nameiparent == TRUE，返回上一级
>   // name是路径里最后一个元素
>   ```
>
> - 首先回顾&预习一下以下函数：
>
>   ````c
>   usize inode_look_up(Inode* inode, const char* name, usize* index)
>   // 在目录inode下寻找文件名为'name'的inode，返回其inode_no
>   const char *skipelem(const char *path, char *name)
>   // 提取出path中的下一个元素，并拷贝到name中，然后返回在下个元素之后的后续路径
>   ````
>
> * 实现思路：
>
>   * 首先要确认从何处开始寻找
>
>     * 相对路径：不是以`'/'`开头，需要获得当前进程所在目录，这是通过给`Proc`结构体增加了`cwd`成员实现的
>
>       > [!note]
>       >
>       > - [ ] 需要完善`proc.c`中有关`cwd`的修改
>
>     * 绝对路径：从根目录开始寻找
>
>   * 其次，循环调用`skipelem`来解析路径
>
>     > [!note]
>     >
>     > 参考了xv6的实现

---

> 小插曲：这里尝试跑通之前的测试时，发现会输出
>
> ````shell
> (warn) init_inodes: no root inode.
> ````
>
> 原因是我们没有从SD卡读取超级块，回忆lab4.2，结合SD卡和磁盘布局：
>
> ```shell
> // SD card distribution
> /*
>  512B            FAT32         file system
> +-----+-----+--------------+----------------+
> | MBR | ... | boot partion | root partition |
> +-----+-----+--------------+----------------+
>  \   1MB   / \    64MB    / \     63MB     /
>   +-------+   +----------+   +------------+
> */
> 
> // disk layout:
> // [ MBR block | super block | log blocks | inode blocks | bitmap blocks | data blocks ]
> ```
>
> 完善`init_block_devide`函数
>
> ```c
> void init_block_device()
> {
>     // read super block from SD card
>     Buf b;
>     b.flags = 0;
>     b.block_no = (u32)0x0;
>     virtio_blk_rw(&b);
>     u8 *data = b.data;
>     int LBA = *(int *)(data + 0x1CE + 0x8);
>     sd_read((usize)(LBA + 1), sblock_data);
>     // print_superblock();
>     block_device.read = sd_read;
>     block_device.write = sd_write;
> }
> ```
>
> 这里的`print_superblock`是为了打印读取后的超级块信息以验证读取是否正确，打印信息如下：
>
> ```shell
> Super Block:
> ---------------------
> num_blocks: 1000
> num_data_blocks: 908
> num_inodes: 200
> num_log_blocks: 63
> log_start: 2
> inode_start: 65
> bitmap_start: 91
> ---------------------
> ```

---

