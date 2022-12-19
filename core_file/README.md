### 定位问题所在

`gdb -c core ngx_mem_pool`

![image-20221219160918277](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212191609400.png)

最初直接编译为 64 位程序，运行后触发段错误，通过调试其 core 文件，发现问题发生在 `large->alloc = p` 赋值语句处，进一步检查 large 这个指针的值，看到了一个 `4` 字节的指针，在 `64` 位环境下显然是错误的

进一步跟进发现，问题出现在以下所述的表达式宏中

### 问题的产生

最初移植完代码后，没有考虑到操作系统位数的问题，导致以下用于调整指针 `p` 到 `a` 的临近倍数的表达式宏出现丢失精度问题：

```C++
#define ngx_align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))
```

其中 

- `uintptr_t`  是 `unsigned int` 的 `typedef` 
- `u_char *` 是 `char *` 的 `typedef`

所以宏之中发生了 `int -> char *` 的强转：

- `32` 位环境中是不存在问题的，因为都为 `4` 字节
- `64` 位环境中存在问题：由于指针 `8` 字节，而 `int` 为 `4` 字节，所以引发精度丢失

### 解决

x64 环境中直接将 `uintptr_t` 直接定义为 `unsigned long int`



