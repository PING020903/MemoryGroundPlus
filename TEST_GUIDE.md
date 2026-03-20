# 内存池测试套件使用说明

## 快速开始

### 1. 运行测试
只需在 `main.c` 中调用综合测试函数：

```c
#include "memGroundP.h"

unsigned char __DBG_string[512] = {0};

int main(void)
{
    // 一键运行所有测试
    mgp_run_all_tests();
    
    return 0;
}
```

### 2. 编译项目
```bash
# Windows PowerShell
.\build.bat

# 或者直接运行
cd build
cmake .. -G Ninja
cmake --build .
```

### 3. 查看测试结果
编译完成后自动运行测试，输出格式如下：

```
╔══════════════════════════════════════════╗
║   Memory Ground Plus - Test Suite       ║
╚══════════════════════════════════════════╝

>>> Running Level 1: Basic Functionality Tests

=====================================
Running test_001_pool_create_normal...
=====================================
[PASS] test_001_pool_create_normal

...

══════════════════════════════════════════
           TEST SUMMARY REPORT            
══════════════════════════════════════════
Total Tests:     21
Passed:          20
Failed:          1
Pass Rate:       95.2%

Failed Tests:
  - test_xxx_failed
══════════════════════════════════════════

✗ SOME TESTS FAILED!
```

## 测试用例列表

### Level 1 - 基础功能测试 (4 个)
- ✅ `test_001_pool_create_normal` - 正常创建内存池
- ✅ `test_002_pool_create_min_size` - 最小尺寸创建
- ✅ `test_003_pool_create_too_small` - 尺寸不足处理
- ✅ `test_004_max_alloc_size_query` - 最大可分配尺寸查询

### Level 2 - 内存分配测试 (4 个)
- ✅ `test_010_single_alloc_small` - 小尺寸分配
- ✅ `test_011_single_alloc_large` - 大尺寸分配
- ✅ `test_012_multiple_allocations` - 多次连续分配
- ✅ `test_013_alloc_until_full` - 分配直到耗尽

### Level 3 - 内存释放测试 (4 个)
- ✅ `test_020_single_free` - 单次释放
- ✅ `test_021_random_order_free` - 乱序释放
- ✅ `test_022_double_free` - 重复释放检测
- ✅ `test_023_free_invalid_address` - 无效地址释放检测

### Level 4 - 碎片整理测试 (2 个)
- ⚠️ `test_030_fragmentation_reuse` - 碎片再利用验证
- ⚠️ `test_031_fragment_size_matching` - 不同尺寸碎片适配

### Level 5 - 边界条件测试 (4 个)
- ⚠️ `test_040_zero_length_alloc` - 零长度分配（跳过）
- ⚠️ `test_041_tiny_alloc` - 极小尺寸分配
- ⚠️ `test_042_max_size_alloc` - 最大尺寸分配
- ⚠️ `test_043_over_max_size_alloc` - 超限分配

### Level 6 - 数据完整性测试 (3 个)
- ⚠️ `test_050_guard_mark_protection` - 头尾标记保护
- ⚠️ `test_051_buffer_overflow` - 缓冲区溢出测试
- ⚠️ `test_052_data_isolation` - 多块数据独立性

### Level 7 - 压力测试 (2 个)
- ⚠️ `test_060_stress_small_allocs` - 大量小块分配
- ⚠️ `test_061_random_mixed_operations` - 随机混合操作

**总计：21 个测试用例**

## 测试配置

### 调试开关
在 `mgp_test.c` 顶部可以配置调试输出：

```c
#define MGP_DEBUG 1
#define MGP_CREATE_POOL_DEBUG 0    // 内存池创建调试
#define MGP_CTRL_BLOCK_DEBUG 0     // 控制块调试
#define MGP_FREE_BLOCK_DEBUG 0     // 空闲块查找调试
#define MGP_MEMORY_FREE_DEBUG 1    // 释放操作调试
#define MGP_MEMORY_ALLOC_DEBUG 1   // 分配操作调试
```

### 自定义测试
如需添加新的测试用例：

1. 在 `mgp_test.c` 中添加测试函数：
```c
static void test_xxx_custom_test(void)
{
    // 测试代码
    TEST_ASSERT(condition, "Error message");
}
```

2. 在 `mgp_run_all_tests()` 函数中注册：
```c
RUN_TEST(test_xxx_custom_test);
```

## 已知问题

### ⚠️ 注意事项
1. **test_040_zero_length_alloc**: 零长度分配会触发 assert，这是预期行为
2. **test_030/031**: 碎片整理测试在某些情况下可能不稳定
3. **test_042**: 最大尺寸分配需要确保内存池足够大

### 🔧 修复建议
如果遇到测试失败：
1. 检查调试输出中的详细信息
2. 确认内存池大小是否满足测试需求
3. 查看 assert 失败的具体位置和原因

## 文件结构

```
memoryGroundPlus/
├── include/
│   ├── memGroundP.h      # 主头文件（包含测试函数声明）
│   └── DBG_macro.h       # 调试宏定义
├── sources/
│   ├── main.c            # 主程序入口
│   ├── memGroundP.c      # 内存池实现
│   ├── mgp_test.c        # 测试套件（新增）
│   └── DBG_macro.c       # 调试宏实现
├── build/
│   └── bin/
│       └── outputFile.exe # 编译生成的可执行文件
└── CMakeLists.txt        # CMake 配置文件
```

## 扩展测试

### 添加性能测试
```c
static void test_perf_allocation_speed(void)
{
    unsigned char mem[4096];
    mgp_t pool = mgp_create_with_pool(mem, sizeof(mem));
    
    // 计时开始
    uint32_t start = get_tick_count();
    
    for (int i = 0; i < 1000; i++) {
        void *block = mgp_malloc(pool, 32);
        mgp_free(pool, block);
    }
    
    // 计时结束
    uint32_t elapsed = get_tick_count() - start;
    DEBUG_PRINT("1000 alloc/free cycles in %u ms", elapsed);
}
```

### 添加内存泄漏检测
```c
static void test_leak_detection(void)
{
    unsigned char mem[1024];
    mgp_t pool = mgp_create_with_pool(mem, sizeof(mem));
    
    // 故意不释放某些块
    void *leak = mgp_malloc(pool, 100);
    
    // 在最后检查是否有未释放的块
    // （需要添加检测函数 mgp_get_alloc_count）
    int leaked = mgp_get_alloc_count(pool);
    TEST_ASSERT(leaked == 0, "Memory leak detected!");
}
```

## 最佳实践

1. **定期运行测试**: 每次修改内存池代码后都应运行完整测试
2. **关注边界条件**: 边界情况最容易暴露问题
3. **启用调试输出**: 失败时详细的调试信息能帮助快速定位问题
4. **逐步增加测试**: 先通过 P0 核心测试，再完善其他测试

## 联系与反馈

如有问题或建议，请查看测试输出中的详细日志信息。
