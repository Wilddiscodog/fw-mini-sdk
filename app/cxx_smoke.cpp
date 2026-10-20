/*
 * C++ 支持冒烟测试 —— 仅在 MINI_CXX_SUPPORT=1 时参与编译/链接
 * 验证: 全局对象构造(cpp_run_init) / new-delete / 虚函数 三大件
 */
#include <stdio.h>

class CxxSmoke {
public:
    CxxSmoke() : ready_(true) { count_++; }
    virtual ~CxxSmoke() {}
    virtual int value() { return 40 + count_; }
    static int count_;
    bool ready_;
};

int CxxSmoke::count_ = 0;
static CxxSmoke g_smoke;   /* 全局对象: 构造函数应被 cpp_run_init() 执行 */

extern "C" int cxx_smoke_test(void)
{
    CxxSmoke *p = new CxxSmoke();   /* operator new -> malloc */
    int v = p->value();             /* 虚调用 */
    bool ok = p->ready_;
    delete p;                       /* operator delete -> free */

    printf("[cxx] smoke: global_ctor=%d new_delete=%d virtual_value=%d %s\n",
           g_smoke.ready_ ? 1 : 0, ok ? 1 : 0, v,
           (g_smoke.ready_ && v == 42) ? "PASS" : "FAIL");
    return (g_smoke.ready_ && v == 42) ? 0 : -1;
}
