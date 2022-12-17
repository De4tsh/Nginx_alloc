# Nginx 内存池整体构造
![Nginx_内存池.drawio](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212171526300.png)

# 基本介绍

`#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)`

能从内存池中分配的最大内存

其中 `ngx_pageszie = 4096` 所以说明 `nginx` 中小块内存与大块内存的区分的界限在于是否大于一个页面

`#define NGX_DEFAULT_POOL_SIZE   (16 * 1024)`

默认池的大小

`#define NGX_POOL_ALIGNMENT    16`

内存池分配时字节对齐的单位

![image-20221215155609829](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151556875.png)

```C
// 与 SGI STL 中的函数一致，将需要的内存的大小调整到临近 a 的倍数上
#define ngx_align(d, a)     (((d) + (a - 1)) & ~(a - 1))

#define NGX_POOL_ALIGNMENT       16

#define NGX_MIN_POOL_SIZE                                         ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t),  NGX_POOL_ALIGNMENT)
// 将逗号前的计算结果调整到 NGX_POOL_ALIGNMENT 也就是 16 的临近倍数

```

内存池最小的大小

### 相关函数接口功能

![image-20221215155942109](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151559172.png)

## 内存池的创建

创建内存池后，其返回类型为 `ngx_pool_t` 类型，所以先来看该类型

```C
typedef struct ngx_pool_s            ngx_pool_t;

typedef struct {
    u_char               *last;
    u_char               *end;
    ngx_pool_t           *next;
    ngx_uint_t            failed; // 内存分配失败次数
} ngx_pool_data_t;

// 创建一个内存池后，该结构体位于内存池首部
struct ngx_pool_s {
    ngx_pool_data_t       d;
    size_t                max;
    ngx_pool_t           *current;
    ngx_chain_t          *chain;
    ngx_pool_large_t     *large;
    ngx_pool_cleanup_t   *cleanup;//相当于一个析构函数，在释放内存池之前执行释放其他的外部资源
    ngx_log_t            *log;
};
```

所以其整体结构为：

![image-20221215164146860](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151641908.png)

### 内存池的创建：

// 下述创建了：内存池中的一个内存块 暂时不确定

![image-20221215160121873](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151601936.png)

```C
p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);
if (p == NULL) 
{
   return NULL;
}
```

**该函数的作用为：根据用户指定的大小开辟内存池，默认直接使用 `malloc` 进行分配，也可以使用 `Linux` 中提供的可以自定义对齐值得动态分配函数 `posix_memalign` 或 `memalign` 来分配内存，对齐值设为 16**

其中 `NGX_POOL_ALIGNMENT` 就是刚刚内存的对齐数 `16` 

其中关于 `ngx_memalign` Linux 下的定义为：

![image-20221215161323363](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151613409.png)

其含义为若没有定义上述两个宏之一，则走 `#define` 的 `ngx_memalign` 

若没有定义上述两个宏就表示不进行内存对齐，则直接走第二个宏函数，调用 `ngx_alloc` 这个函数

![image-20221215161752834](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151617885.png)

该函数就是直接调用 `malloc` 进行分配内存，没有进行内存对齐

若进行对齐则调用系统函数 `posix_memalign` 或 `memalign` 这两个可以自定义对齐值的内存分配函数进行对齐分配动态内存

其中之一，另一个函数调用方式一致：

![image-20221215162257785](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151622846.png)

- `alignment`    对齐边界，`Linux` 中，`32` 位系统是 `8` 字节，`64` 位系统是 `16` 字节（此处可自行指定）

内存池基本参数的初始化：

```C
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    p->d.end = (u_char *) p + size;
    p->d.next = NULL;
    p->d.failed = 0;

    size = size - sizeof(ngx_pool_t);
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p;
    p->chain = NULL;
    p->large = NULL;
    p->cleanup = NULL;
    p->log = log;
```

- `last = (u_char *) p + sizeof(ngx_pool_t);` 

  `last` 指针指向了内存池头部的结尾，也就是刚刚结构体中最后一个参数 `log` 的下方，已使用部分之前

  <img src="https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151641585.png" alt="image-20221215164104527" style="zoom: 50%;" />

- `end = (u_char *) p + size`

  `end` 指向整个内存池的末尾

  <img src="https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151646880.png" alt="image-20221215164648816" style="zoom:50%;" />

  所以 `end - last` 就是用户可以使用的空间

- `next = NULL;`

  初始化第一个内存池（中的内存块）时，`next` 指向空，之后若在产生另一个内存池（块），则会指向下一个内存池（块）的地址

- `p->d.failed = 0;` 后面再说

- `size = size - sizeof(ngx_pool_t);`

  可使用的大小，用内存之的总大小 - 内存池头信息

  上图中 **已使用 + 未使用** 部分的总大小

- `max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;`

  `max` 表示当前内存池最大可用空间大小，最大为 `4095`

  若 `size < 4095` 可用空间小于一个页的大小，则当前最大大小就是 `size`

  若 `size > 4095`  则用一个页面 - 1 的大小 `4095` 也就是当前小块内存可以分配的最大值（因为 `Nginx` 内存池也是主要负责小块内存的分配回收与管理，而 `Nginx` 规定 `<= 4095` 才属于小块内存）

- `current = p;`

  `current` 指针指向当前内存池头部，也就是当前内存池的首地址

- ```C
  p->chain = NULL;
  p->large = NULL;
  p->cleanup = NULL;
  ```

  都为空

## 向内存池申请内存

```C
// 考虑内存对齐
void *ngx_palloc(ngx_pool_t *pool, size_t size);
// 不考虑内存对齐
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
// 分配了进行初始化清0
void *ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    // 调用 palloc 并清 0
    void *p;

    p = ngx_palloc(pool, size);
    if (p) 
    {
        ngx_memzero(p, size);
    }

    return p;
}    
```

主要分析 `ngx_palloc`

### `ngx_palloc`

![image-20221215170344703](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151703747.png)

- `ngx_pool_t *pool` 该参数指向的就是刚刚开辟的内存池的首地址
- `size_t size` 要申请内存的大小

```C
if (size <= pool->max) 
{
    return ngx_palloc_small(pool, size, 1);
}

```

若申请的内存 `< pool->max` 也就是说小于当前内存块的上限（ `max` 最大为 `4095` ）则进入 `ngx_palloc_small` 这个小块内存的分配函数

```C
#endif

    return ngx_palloc_large(pool, size);
}
```

否则进入 `ngx_palloc_large` 大块内存分配函数

#### `ngx_palloc_small`

![image-20221215171334746](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151713802.png)

- `p = pool->current;` 

  首先让 `p` 指向当前正在使用内存池的开头

- `m = p->d.last;`

  `m` 指向可分配内存的起始地址

- 若考虑内存对齐

  ```C
  #define ngx_align_ptr(p, a)                                                   \
      (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))
  
  // 与所处平台有关 32位4字节 64位8字节
  #define NGX_ALIGNMENT   sizeof(unsigned long)    /* platform word */
  
  if (align) 
  {
      // 将 m 指针这个地址根据所处平台的不同调整为 4 字节或 8 字节整数倍的地址上
      m = ngx_align_ptr(m, NGX_ALIGNMENT);
  }
  ```

- `(size_t) (p->d.end - m) >= size`

  `p->d.end - m` 用当前内存池中 `end - last` 指针获得当前内存池中可用空间的大小

  <!-- 得到除去头部以外可用空间的总大小其最大值为 p->max -->

  所以这个判断的含义是：若当前内存池中剩余的可用空间大小 `>` 请求的空间 `size` 则可以申请，接下来进行申请：

  ```C
  p->d.last = m + size;
  return m;
  ```

  通过下移 `last` 指针来申请空间，移动后 `last` 指针指向下一块可以分配空间的开头，而从 `m - last` 指针中间的区域就是当前分配出给用户使用的 `size` 大小的空间

  <img src="https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151732097.png" alt="image-20221215173213036" style="zoom:50%;" />

所以当前直接可以分配成功的流程就是：

- 通过 `current` 找到当前使用的内存块
- 通过 `last` 找到当前可用空间的开始指针
- 通过 `end - last` 算出可用空间是否够申请的大小
- 若足够则通过下移 `last` 指针分配出空间

**若剩余的可用空间不够分配了，也就是 `end -last < size`**

```C
p = p->d.next;
```

则会尝试找到下一个内存块，但当前刚刚初始化，所以只有一个内存块，此处就会进入`ngx_palloc_block(pool, size);` 函数

![image-20221215174137188](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151741257.png)

- `psize = (size_t) (pool->d.end - (u_char *) pool);`

  用刚刚内存块的 `end` 指针 - 刚刚内存块的起始地址得到了刚刚内存块的总大小，将作为新开辟内存块的大小，所以新开辟的内存块和原内存块大小一致

- 开辟空间

  ```C
  m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);
  if (m == NULL) 
  {
     return NULL;
  }
  ```

  此处和上面分配空间的函数一致，默认也是直接使用 `malloc` 进行分配，并让 `m` 指向这块新空间的起始地址

  `new = (ngx_pool_t *) m;` `new` 指针也指向新开辟内存池的开头

- 为新的内存块添加头部

  ```C
  new->d.end = m + psize;
  new->d.next = NULL;
  new->d.failed = 0;
  
  m += sizeof(ngx_pool_data_t);
  m = ngx_align_ptr(m, NGX_ALIGNMENT);
  new->d.last = m + size;
  ```

  - `new->d.end = m + psize;` 新内存块的 `end` 指针指向该内存块结尾（ `m` 是头指针 `+ psize` 该内存块的大小）

  - `new->d.next = NULL;` 当前是最后一个节点

  - `new->d.failed = 0;`

  - `m += sizeof(ngx_pool_data_t);` m = 当前内存块首地址 + 当前内存块头部的大小

    ```C
    typedef struct {
        u_char               *last;
        u_char               *end;
        ngx_pool_t           *next;
        ngx_uint_t            failed;
    } ngx_pool_data_t;
    ```

    说明从第二个内存块开始，内存头信息中就不存在 `max` 这些信息了，只有 `last`、`end`、`next`、`failed` 四个头信息

    其余头信息仅在第一个内存块中存在

  - `new->d.last = m + size;` 

    由于刚刚再上一个参数中空间不足 size 无法分配时才会跳到当前函数再申请内存块，所以当前就是通过下移 `last` 指针 `size` 个偏移，为刚刚申请的 `size` 大小的空间分配出内存

    <img src="https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212151759451.png" alt="image-20221215175923392" style="zoom:50%;" />

  - 串联内存块

    ```C
    for (p = pool->current; p->d.next; p = p->d.next) 
    {
        if (p->d.failed++ > 4) 
        {
            pool->current = p->d.next;
        }
    }
    p->d.next = new;
    ```

    当前才分配到第二个内存块，所以进不到 `for` 循环中 （原因在于 `p->d.next = NULL`）

    所以直接执行 `p->d.next = new;` 也就是将第一个内存块的 `next` 指针指向当前第二个内存块的首地址，将他们串成单链表

    <img src="https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212161624059.png" alt="image-20221216162400865" style="zoom:50%;" />

    但要注意，每次分配都是从 `current` 指针指向的位置分配的，而该操作并没有改变 `current` 指针的指向，所以若当前第二个内存块不够，需要再申请时，还是从第一个内存块开始遍历寻找哪个内存块有合适大小的空间

    但是通过该函数来重新申请分配内存就说明上面内存块分配失败了：

    ```C
    for (p = pool->current; p->d.next; p = p->d.next) 
    {
        if (p->d.failed++ > 4) 
        {
            pool->current = p->d.next;
        }
    }
    ```

    所以该循环的含义在于，每次因分配失败需要调用该函数重新申请内存块时，都说明前面的内存块中剩余的空间都不足为新申请的来分配，所以就会通过该 `for` 循环，遍历前面所有的内存块，并将其 `failed` 参数 `+1`，表示分配失败一次，若在某次遍历中发现该内存块的失败次数已经 `> 4` 了，那就可以认为当前内存块剩余的空间已经非常少了，此时就会将 `current` 指针指向它的下一个内存块的首地址，以后再分配时就会从该 `current` 指向的位置来寻找可利用的内存块

#### `ngx_palloc_large`

小块内存与大块内存的分界点在于是否 `> max` 

![image-20221216161629108](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212161616352.png)

其中涉及到了一个新的结构体：`ngx_pool_large_t`

![image-20221216162543744](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212161625854.png)

- `ngx_pool_large_t   *next` 链表下一个节点（用链表将大块内存串起来）
- `void         *alloc;` 大块内存的起始地址

```C
p = ngx_alloc(size, pool->log);
if (p == NULL) 
{
   return NULL;
}
```

大内存调用 `ngx_alloc` 开辟内存，其中则是直接调用 `malloc` 来开辟大块的大小为 `size` 的内存

```C
for (large = pool->large; large; large = large->next) 
{
    if (large->alloc == NULL) 
    {
       large->alloc = p;
       return p;
    }

    if (n++ > 3) 
    {
       break;
    }
}
```

此处使用了第一个内存池块中的 `large` 指针，该指针最初也为 `NULL`，所以第一次并不会进入该 `for` 循环，先往下看，看完再回看缩进中的内容

​	每次要申请大块内存的时候，不会都直接开辟一个大块内存的头节点，而是会先遍历已有的所有大块内存头信息，若是其中有某个大块内存头 `alloc` 指针指向的内存已经被 `free` 释放了，则此时该大块内存头是可以复用的，无需才申请一个新的内存头，直接将刚刚 `malloc` 申请的大块内存的指针放入该内存头的 `alloc` 指针域中即可

```C
large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
if (large == NULL) 
{
   ngx_free(p);
   return NULL;
}

large->alloc = p;
large->next = pool->large;
pool->large = large;
```

- `large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);`

  可以看出为了精简，**大块内存的内存头**（`struct ngx_pool_large_t`）**也是直接在小块内存池中进行分配**

  ```C
  if (large == NULL) 
  {
     ngx_free(p);
     return NULL;
  }
  ```

  若小块内存不够导致大块内存头无法分配，则将刚刚 `malloc` 分配的大块内存释放掉

- 串联

  ```C
  // 将大块内存头中的 alloc 指针指向刚刚 malloc 分配出的内存块
  large->alloc = p;
  // 头插法，每次新申请的大块内存头都放在第一个内存池快 large 指针指向的下一个位置，形成单链表
  large->next = pool->large;
  pool->large = large;
  ```

  <img src="https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212161644592.png" alt="image-20221216164412522"  />

（注意其中第二块内存中 last 指针所指的位置不严谨，应为未使用区的顶部）

#### `ngx_free`

![image-20221216182911397](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212161829471.png)

其中调用的 `ngx_free` 函数就是直接使用 `free` 调用释放刚刚 `malloc` 的空间，但并没有释放大块内存头（`nginx` 中关于小块内存的释放是有专门的函数的，后面会说），并再把大块内存头节点的 `alloc` 指针置为 `NULL` 方便上述 `for` 遍历时找到可以复用的头部

## 内存池的重置

刚刚所说的 `ngx_pfree` 仅用来释放大块的内存，并不负责小块内存的释放，由于 `nginx` 内存池中小块内存的分配与标识仅用两个指针 `end` 和 `last` 来标识，所以难以释放与合并回收，当前内存池的重置主要就是解决小块内存的问题

### `ngx_reset_pool`

![image-20221216184200139](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212161842239.png)

两个 `for` 一个遍历大块内存头的链表，一个遍历小块内存头链表

```C
for (l = pool->large; l; l = l->next) 
{
    if (l->alloc) 
    {
        ngx_free(l->alloc);
    }
}
```

释放掉所有大块内存 `malloc` 分配出的空间

```C
for (p = pool; p; p = p->d.next) 
{
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    p->d.failed = 0;
}
```

- `p->d.last = (u_char *) p + sizeof(ngx_pool_t);`

  对于首内存池，该操作没问题，将 `last` 指针重新指回小块内存头部末尾的地方，相当于释放掉了所有小块内存的空间

  但对于后续的内存块中，其小内存块头部中并不包含 `ngx_pool_t` 结构体中的所有参数，从 `max` 以及往后的这些负责管理的参数只有第一个小块内存的头信息中包括，所以此处其实不对，但并不会导致程序异常，指示每次都有一部分空间没有释放，可能会有风险

  所以上述代码应改为：

  ```C
  // 释放第一个管理内存池
  p = pool;
  p->d.last = (u_char *) p + sizeof(ngx_pool_t);
  p->d.failed = 0;
      
  // 其余内存池
  for (p = pool; p; p = p->d.next) 
  {
      p->d.last = (u_char *) p + sizeof(ngx_pool_data_t);
      p->d.failed = 0;
  }
  ```

接下来重置 current 指针，指向第一个块等即可

```C
pool->current = pool;
pool->chain = NULL;
pool->large = NULL;
```

#### 为什么要提供该函数

由于 `nginx ` 本质大多数场景中是作为 `http` 服务器，而由于 `HTTP` 协议无状态，所以 `Nginx` 作为的是一个短连接的服务器，客户端发起一个 `request`  请求，到达 `nginx` 服务器以后，经后端处理完成后，`nginx` 会给客户端返回一个 `response` 响应，`HTTP` 服务器就主动断开 `tcp` 连接 ( 若设置了  `http 1.1 keep-alive` ) ，则 `HTTP`  服务器 ( `nginx` ）返回响应以后，需要等待 `60s`，若 `60s`  之内容户端又发来请求，则重置这个时间继续等待，否则 `60s` 之内没有客户端发来的响应，`nginx` 就主动断开连接，此时  `nginx`  便可以调用  `ngx_reset_pool`  当前连接的重置内存池，等待下一次客户端的请求再创建

# cleanup

在申请了一个大块内存后，这个大块内存其中的代码或某些操作可能会在其中又在堆区申请了一些空间，那么在释放/销毁整个内存池的时候，也要释放这些在大块内存中又在堆区中开辟了空间的空间指针，但 `C` 并不提供析构函数，无法在外部被销毁时自动调用析构函数完成上述善后工作，所以 `Nginx` 会提供一个句柄（回调函数指针），但该释放函数需要用户自定义，之后将其挂到回调函数指针上，之后在 Nginx 要释放内存池的时候便会调用该用户自定义的函数，其目的就是：释放用户自己又开辟的堆空间

在 `Nginx` 内存池中，这个任务就交给了 `cleanup` 来实现

```C
typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s  ngx_pool_cleanup_t;

struct ngx_pool_cleanup_s 
{
    ngx_pool_cleanup_pt   handler;
    void                 *data;
    ngx_pool_cleanup_t   *next;
};

```

- `ngx_pool_cleanup_pt   handler;` 

  该函数指针代表了一个回调函数，就负责在内存池被释放的时候清理释放其中可能存在的未释放的堆空间

- `void                 *data;` 要释放的资源的指针

- `ngx_pool_cleanup_t   *next;` 将所有释放动作串在一个链表中

![Nginx_内存池.drawio](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212171526300.png)

## ngx_pool_cleanup_add

<img src="https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212171527247.png" alt="image-20221217152717079" style="zoom:67%;" />

- 在小块内存区开辟 `ngx_pool_cleanup_s` 头部

  ```C
  c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
  if (c == NULL) 
  {
      return NULL;
  }
  ```

- 若向清理函数传递了 `size` ，则调用 `ngx_palloc` 为其开放 `size` 大小的空间，否则默认情况下将 `data` 指针置空

  ```C
  if (size) 
  {
     c->data = ngx_palloc(p, size);
     if (c->data == NULL) 
     {
         return NULL;
     }
  } 
  else 
  {
       c->data = NULL;
  }
  ```

- 由于当前还没有指定回调函数，所以将其置为 `NULL` ，同时将当前的 `cleanup` 头部信息块，串入 `cleanup` 头部信息块的链表（头插法）

  ```C
  c->handler = NULL;
  c->next = p->cleanup;
  
  p->cleanup = c;
  ```

- 最终返回该清理结构体头部地址

其中外部资源的释放函数是需要用户根据其是否开辟了堆空间来决定写些什么内容的，写完后只需将其函数指针挂到上述清理函数头信息中的 `handler` 指针处即可，这样在 `Nginx` 释放内存池的时候便会调用一次该函数

假如用户写了如下释放自己在函数中又申请的堆空间的释放函数

```C
int *p = (int *)malloc(1024);
void release(void *p)
{
    free(p);
}
```

那么要想让 `Nginx` 在释放内存池的时候调用该函数，只需

```C
ngx_pool_cleanup_t *pclean = ngx_pool_clean_up_add(pool,sizeof(char *));
pclean->handler = &release;
pclean->data = p;
```

## ngx_destroy_pool

释放内存池：

- 先执行用户自定义的预制的回调函数清理用户自己申请的外部堆空间
- 释放内存池中的大块内存
- 释放小块内存

![image-20221217155509791](https://raw.githubusercontent.com/De4tsh/typoraPhoto/main/img/202212171555995.png)

- 调用清理函数 `handler` 释放用户自己开辟的外部堆空间

  ```C
  for (c = pool->cleanup; c; c = c->next) 
  {
     if (c->handler) 
     {
        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,"run cleanup: %p", c);
        c->handler(c->data);
        // 相当于 realease(p);
     }
  }
  ```

  遍历 `cleanup` 链表的所有节点，只要其中定义了 `handler` 就将 `data` 指针作为参数调用 `handler` 从而调用用户自定义的清理函数来回收用户自己申请的堆空间

  也就会在释放掉内存池申请的大块内存资源前，先释放用于自己在其中又申请的外部堆空间

- 释放 `Nginx` 大块内存池资源

  ```C
  for (l = pool->large; l; l = l->next) 
  {
      if (l->alloc) 
      {
         ngx_free(l->alloc);
      }
  }
  ```

- 释放 `Nginx` 小块内存池资源

  ```C
  for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) 
  {
      ngx_free(p);
  
      if (n == NULL) 
      {
         break;
      }
  }
  ```

指向用户定义的外部资源
