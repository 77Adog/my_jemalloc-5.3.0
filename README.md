# MyJemalloc

The  jemalloc-5.3.0 that I modify

## 访问 NVM 的方式

实验室的服务器中，NVM 装载在目录 `/mnt/pmen0` 和 `/mnt/pmen1` 下

1. 在某个 NVM 装载的目录中创建文件.
2. 使用 `posix_fallocate(fd, offset, size)` 函数预分配一定的空间，使得文件有一定的空间，从而不会在 `mmap` 时出现 Bus error。
3. 在分配器进程中，使用 `open` 打开 NVM 中的文件，根据 nvalloc 代码中，打开的代码如下所示。
```c
int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
```
其中各个 flags 的意义如下
- `O_RDWR`: 表示文件可读可写
- `O_CREAT`: 若路径不存在，则创建一个常规的文件。
- `O_TRUNC`: 若文件已经存在且允许写时，会将文本的长度设置为 0，内容全部丢弃 (这里可能是做 benchmark 的时候进行的设计, 若加上该 flag 则一定要调用 `posix_fallocate` 来预先分配一定的空间)
各个 mode 的意义如下
- `S_IRUSR`: 所有者拥有读权限。
- `S_IWUSR`: 所有者拥有写权限。
1. 打开文件后会得到对应的文件描述符，使用 `mmap` 系统调用将该文件映射到系统空间中，nvalloc 的代码中，映射的代码如下所示
```c
void *addr = mmap(0, size, PROT_READ | PROT_WRITE, 0x80003, fd, 0);
```
其各个参数的意思为
- 第一个参数 0 表示：映射不指定虚拟地址，由操作系统分配映射的虚拟地址
- 第二个参数 size 表示：创建映射的长度，
- 第三个参数为 prot 参数，表示该映射的 memory protection，此处的参数表示映射的 page 可能被读和写
- 第四个参数为 flags，flags 参数确定对映射的更新是否对映射同一区域的其他进程可见，以及更新是否传递到基础文件。
- - `MAP_SHARED_VALIDATE`：即 0x3，This flag provides the same behavior as MAP_SHARED except
              that MAP_SHARED mappings ignore unknown flags in flags.
              By contrast, when creating a mapping using
              MAP_SHARED_VALIDATE, the kernel verifies all passed flags
              are known and fails the mapping with the error EOPNOTSUPP
              for unknown flags.  This mapping type is also required to
              be able to use some mapping flags (e.g., MAP_SYNC).
- - `MAP_SYNC`：即 0x80000，有上述 flag 时才生效，只有在映射 DAX 文件系统的文件时才生效，作用是保证修改是持久化的，其保证写入该内存映射区域后，若发生了系统 crush，则文件中相同偏移量的位置还是能读到之前写过的内容。
- 第五个参数是文件描述符
- 第六个参数是打开文件的偏移量（offset must be a multiple
       of the page size as returned by sysconf(_SC_PAGE_SIZE).）
1. 此时即可将数据写在对应的虚拟内存空间即可将数据写在持久化
2. 要关闭文件时，先使用系统调用 `munmap` 函数，将映射释放掉，用法为 `munmap(addr, size)`
3. 再使用 `close` 函数关闭文件，用法为 `close(fd)`

## je_malloc 最坏情况的调用结构

- 调用 malloc 的接口为 `je_malloc` 函数
- 在 tcache 找不到时，会调用 `malloc_default` 函数
- 在 `malloc_default` 中会调用 `imalloc` 函数来分配，其后续的 `hook_invoke_alloc` 已经被优化掉了，不用管
- 在 `imalloc` 中会调用 `imalloc_body` 来完成其功能
- `imalloc_body` 中所有的 `goto` 都是产生错误的地方，
- `imalloc_body` 应该有调用 `imalloc_no_sample` 和 `imalloc_sample` 两个分支
- - `imalloc_no_sample`：又有两个分支，对其就走 `ipalloct`，不对齐就走 `iallocztm`
- - - `ipalloct`：直接调用 `ipallocztm`
- - - `ipallocztm`：会调用 `arena_palloc` 来分配
- - - `arena_paclloc` 对于大块的两个分支，最后都会调用 `large_palloc` 
- - - `iallocztm`：
- - `imalloc_sample`：

(实在分析不下来了)

## 从下至上的分析调用结构

思路：查找用到了 `mmap` 和 `brk` 等系统调用的函数，一直向上找到第一个分叉的函数，作为修改为持久内存的接口

