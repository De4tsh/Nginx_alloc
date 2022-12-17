![image-20221217213645794](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212172136881.png)

从图中可知，我们在通过 `ngx_palloc` 申请的大块内存 `p2` 中，又通过 `malloc` 与 `fopen` 申请了额外的堆（方式不同）空间，得到了两个指针：

- `p2->ptr` 字符串堆指针
- `p2->pfile` 文件描述符指针

并且自定义了释放函数：

- `func1`  释放字符串所在堆空间
- `func2` 释放文件描述符

由于 `C` 语言不存在类与析构函数的调用，所以在内存池被释放时，默认情况下是无法调用两个释放函数的

所以此时通过 `ngx_pool_cleanup_add()` 函数将两个释放函数的函数指针与参数通过创建 `ngx_pool_cleanup_t` 钩起来（将用户自定义的函数作为回调函数），在调用 `ngx_destory_pool()` 时会首先遍历 `ngx_pool_cleanup_t` 结构体构成的链表，依次回调其中保存的函数指针与对应函数的参数释放掉用户自己申请的堆空间后，才会清理自身内存池中的大块内存与小块内存
