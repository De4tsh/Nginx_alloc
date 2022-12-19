#include "ngx_mem_pool.h"
using namespace std;

typedef struct Data stData;
struct Data
{
    char *ptr;
    FILE *pfile;
};

void func1(void *p)
{
    char *p_ = (char *)p;
    cout << "User out ptr memory:" << (void *)p_ << endl;
    cout << "free ptr mem!" << endl;
    free(p_);
}
void func2(void *pf)
{
    FILE *pf_ = (FILE *)pf;
    cout << "User out file ptr:" << pf_ << endl;
    cout << "close file!" << endl;
    fclose(pf_);
}
int main(int argc,char** argv)
{

    // 由构造函数直接调用 create_pool 创建内存池
    ngx_mem_pool mempool(512);

    void *p1 = mempool.ngx_palloc(128); // 从小块内存池分配的
    if(p1 == nullptr)
    {
        cout << "ngx_palloc 128 bytes fail..." << endl;
        return 1;
    }

    stData *p2 = (stData *)mempool.ngx_palloc(512); // 从大块内存池分配的
    if(p2 == nullptr)
    {
        cout << "ngx_palloc 512 bytes fail..." << endl;
        return 1;
    }

    // 指向用户自定义的外部资源
    p2->ptr = (char *)malloc(12);
    strcpy(p2->ptr, "hello world");
    p2->pfile = fopen("data.txt", "w");
    
    ngx_pool_cleanup_s *c1 = mempool.ngx_pool_cleanup_add(sizeof(char*));
    c1->handler = func1;
    c1->data = p2->ptr;

    ngx_pool_cleanup_s *c2 = mempool.ngx_pool_cleanup_add(sizeof(FILE*));
    c2->handler = func2;
    c2->data = p2->pfile;

    // 清理函数以挂入析构函数中
    // 1.调用所有的预置的清理函数
    // 2.释放大块内存
    // 3.释放小块内存池所有内存

    return 0;
}
