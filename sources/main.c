#include "memGroundP.h"

unsigned char __DBG_string[512] = {0};
__attribute__((aligned(4)))static unsigned int memPool[0x400 / sizeof(unsigned int)] = {0};

int main(void)
{
    // 方式一：运行综合测试套件（推荐）
    //mgp_run_all_tests();
    
    // 方式二：运行原有的简单测试
    mgp_t handle = mgp_create_with_pool(memPool, sizeof(memPool));
    mgp_test(handle);
    
    return 0;
}