/**
 * @file memGroundP.c
 * @brief 内存池管理模块实现文件
 * @details 实现基于预分配内存的内存池管理功能
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "memGroundP.h"
#if MGP_DEBUG
#include <assert.h>
#include "DBG_macro.h"

#define MGP_CREATE_POOL_DEBUG 0
#define MGP_CTRL_BLOCK_DEBUG 0
#define MGP_FREE_BLOCK_DEBUG 0
#define MGP_MEMORY_FREE_DEBUG 0
#define MGP_MEMORY_ALLOC_DEBUG 0
#else
static void assert(void *p) {}
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

/** @defgroup MGP_INTERNAL 内部宏定义
 *  @{
 */
#define HGIH_TO_LOW 0                     ///< 从高到低排序模式
#define LOW_TO_HIGH 1                     ///< 从低到高排序模式
#define CTRL_BLOCK_QSORT_MDOE HGIH_TO_LOW ///< 控制块快速排序模式（当前设置为从高到低）
/**  @} */

/**
 * @struct mgp_ctrl_t
 * @brief 内存块控制结构
 * @details 用于管理每个分配的内存块，包含缓冲区大小和头尾保护标记位置
 *
 * 内存布局：[头部标记][用户缓冲区][尾部标记]
 * - pHead: 指向头部标记位置
 * - pTail: 指向尾部标记位置
 * - pBuff: 指向用户实际可用的缓冲区起始位置（头部标记之后）
 * - buffSize: 包含头尾标记的总大小
 */
typedef struct
{
    size_t buffSize;     ///< 缓冲区总大小（包含头尾标记）
    unsigned int *pHead; ///< 指向头部保护标记位置
    unsigned int *pTail; ///< 指向尾部保护标记位置
    void *pBuff;         ///< 指向用户可用缓冲区起始位置
    // 多申请 sizeof(unsigned int) 字节用以作为 MGP_tail
} mgp_ctrl_t;

/**
 * @struct mgp_pool_t
 * @brief 内存池控制结构
 * @details 管理整个内存池的状态和信息
 *
 * 内存池布局：[mgp_pool_t][mgp_ctrl_t 数组][已分配的内存块]
 */
typedef struct
{
    mgp_t baseAddr;        ///< 内存池基地址
    size_t totalSize;      ///< 内存池总大小（字节）
    size_t usedSize;       ///< 已使用的大小（字节）
    mgp_ctrl_t *ctrlTable; ///< 控制块表指针
    short allocBlockCnt;   ///< 已分配的内存块数量
} mgp_pool_t;

/**
 * @brief 使用给定的内存区域创建内存池
 * @param[in] mem 指向预分配的内存区域起始地址
 * @param[in] bytes 内存区域的总大小（字节）
 * @return 成功返回内存池句柄，失败返回 NULL
 * @details
 * 初始化流程：
 * 1. 参数检查（内存指针有效性、最小容量检查）
 * 2. 计算内存池控制结构信息
 * 3. 清空整个内存区域
 * 4. 写入控制结构信息
 *
 * 内存布局：
 * - 起始位置：mgp_pool_t 结构体
 * - 紧随其后：mgp_ctrl_t 控制块表
 * - 剩余空间：用于动态分配的内存块
 */
mgp_t mgp_create_with_pool(void *mem, size_t bytes)
{
    assert(mem);
    const size_t memMinSize = sizeof(mgp_pool_t) + sizeof(mgp_ctrl_t); // 池记录表 + 块记录区
    if (bytes < memMinSize)
        return NULL;

    const uintptr_t memHead = (uintptr_t)mem;
    const uintptr_t memTail = (uintptr_t)mem + bytes;
    const mgp_pool_t info = {
        .baseAddr = mem,
        .totalSize = bytes,
        .allocBlockCnt = 0U,
        .usedSize = memMinSize,
        .ctrlTable = (void *)((uintptr_t)mem + sizeof(info))};
    const mgp_pool_t *pInfo = mem;

    memset(mem, 0X00, bytes);
    memcpy(mem, &info, sizeof(info));
#if MGP_CREATE_POOL_DEBUG
    VAR_PRINT_HEX(memHead);
    VAR_PRINT_HEX(memTail);
    VAR_PRINT_UD(pInfo->usedSize);
    VAR_PRINT_UD(pInfo->totalSize);
    VAR_PRINT_UD(pInfo->allocBlockCnt);
    VAR_PRINT_HEX((uintptr_t)pInfo->ctrlTable);
#endif
    return info.baseAddr;
}

/**
 * @brief 控制块比较函数（用于快速排序）
 * @param[in] a 第一个控制块指针
 * @param[in] b 第二个控制块指针
 * @return 按照 pHead 地址大小比较结果
 * @details
 * 根据 CTRL_BLOCK_QSORT_MDOE 配置决定排序方向：
 * - HGIH_TO_LOW: 按地址从高到低排序
 * - LOW_TO_HIGH: 按地址从低到高排序
 * @note 该函数作为 qsort 的比较回调函数
 */
static int mgp_ctrlBlockCompar(const void *a, const void *b)
{
    assert(a);
    assert(b);
    const mgp_ctrl_t *ctrlA = a;
    const mgp_ctrl_t *ctrlB = b;
#if (CTRL_BLOCK_QSORT_MDOE == LOW_TO_HIGH)
    return (ctrlA->pHead > ctrlB->pHead) ? 1 : -1;
#elif (CTRL_BLOCK_QSORT_MDOE == HGIH_TO_LOW)
    return (ctrlA->pHead < ctrlB->pHead) ? 1 : -1;
#else
    return 0;
#endif
}

/**
 * @brief 计算内存池可分配的最大内存块大小
 * @param[in] p 内存池句柄
 * @return 可分配的最大字节数
 * @details
 * 计算公式：totalSize - (sizeof(mgp_pool_t) + sizeof(mgp_ctrl_t) - 2*sizeof(unsigned int))
 * 需要减去池控制结构和控制块的开销
 */
const size_t mgp_canAllocMaxSize(mgp_t p)
{
    assert(p);
    const mgp_pool_t *pool = p;
    const size_t memMinSize = sizeof(mgp_pool_t) + sizeof(mgp_ctrl_t) - ((sizeof(unsigned int)) * 2); // 池记录表 + 块记录区
    return pool->totalSize - memMinSize;
}

/**
 * @brief 检查内存块大小是否越界
 * @param[in] Pool 内存池指针
 * @param[in] addr 内存块起始地址
 * @param[in] needBytes 需要的字节数（包含头尾标记）
 * @return 越界返回 1，未越界返回 0
 * @details 
 * 检查从指定地址开始分配指定字节是否会超出内存池范围。
 * 
 * 计算公式：
 * - 结束地址 = addr + needBytes + sizeof(mgp_ctrl_t) + sizeof(unsigned int)
 * - 如果结束地址 > 内存池最大地址，则判定为越界
 * 
 * @note 该函数用于防止内存块分配时超出内存池边界
 * @warning needBytes 参数应已包含头尾标记的大小
 */
static int mgp_checkBlockSize(mgp_pool_t *Pool, const void *addr, size_t needBytes)
{
    assert(Pool);
    assert(addr);

    const uintptr_t base = (uintptr_t)Pool->baseAddr;
    const uintptr_t maxAddr = base + Pool->totalSize;
    uintptr_t current = (uintptr_t)addr;
    current += (needBytes + sizeof(mgp_ctrl_t) + sizeof(unsigned int));
    return (current < maxAddr) ? 0 : 1;
}

/**
 * @brief 内存碰撞检查
 * @param[in] newCtrl 新的控制块指针
 * @param[in] pBuff 缓冲区指针
 * @return 发生碰撞返回非 0，无碰撞返回 0
 * @details 检查控制块是否与缓冲区地址重叠
 */
static int mgp_MemoryCollisionCheck(const mgp_ctrl_t *newCtrl, const void *pBuff)
{
    assert(newCtrl);
    assert(pBuff);

    uintptr_t ctrlEndAddr = (uintptr_t)newCtrl + sizeof(*newCtrl);
    uintptr_t pBuffAddr = (uintptr_t)pBuff;

    return (ctrlEndAddr <= pBuffAddr) ? 0 : !0;
}

/**
 * @brief 创建控制块并初始化
 * @param[in] pool 内存池指针
 * @param[in] pBuff 缓冲区起始地址
 * @param[in] bufLen 缓冲区长度（包含头尾标记）
 * @return 成功返回控制块指针，失败返回 NULL
 * @details
 * 初始化流程：
 * 1. 检查是否有足够的控制块空间
 * 2. 进行内存碰撞检查
 * 3. 增加分配计数
 * 4. 初始化控制块各字段
 * 5. 写入头尾保护标记
 */
static mgp_ctrl_t *mgp_createWithCtrlBlock(mgp_pool_t *pool, void *pBuff, const size_t bufLen)
{
    assert(pool);
    assert(pBuff);
    assert(bufLen > (sizeof(unsigned int) * 2)); // 缓冲区最小需容纳 2 个魔术数

    int idx = pool->allocBlockCnt;
    if (idx == 0)
        goto _next;
    if (idx < 0)
        return NULL;

    mgp_ctrl_t *newCtrl = &pool->ctrlTable[idx];
    if (mgp_MemoryCollisionCheck(newCtrl, pBuff)) // 控制块与缓冲区碰撞
        return NULL;
_next:
    pool->allocBlockCnt += 1;
    idx = pool->allocBlockCnt - 1;

    pool->ctrlTable[idx].buffSize = bufLen;
    pool->ctrlTable[idx].pHead = pBuff;
    pool->ctrlTable[idx].pTail = (void *)&((unsigned char *)pBuff)[bufLen - sizeof(unsigned int)];
    pool->ctrlTable[idx].pBuff = (void *)((uintptr_t)pBuff - sizeof(unsigned int));

    *(pool->ctrlTable[idx].pHead) = MGP_GUARD_HEAD;
    *(pool->ctrlTable[idx].pTail) = MGP_GUARD_TAIL;
#if MGP_CTRL_BLOCK_DEBUG
    VAR_PRINT_HEX((uintptr_t)newCtrl);
    VAR_PRINT_HEX((uintptr_t)&pool->ctrlTable[idx]);
#endif

    return &pool->ctrlTable[idx];
}

/**
 * @brief 获取空闲内存块
 * @param[in] pool 内存池指针
 * @param[in] bytes 需要的字节数
 * @return 成功返回空闲块指针，失败返回 NULL
 * @details
 * 分配策略：
 * 1. 如果是第一个块，直接从内存池顶部往下分配
 * 2. 对控制块表进行排序（按地址从高到低）
 * 3. 遍历已分配块之间的空隙，寻找合适的空闲块
 * 4. 如果没有合适空隙，返回新的空闲块（最后一个已分配块之前）
 *
 * @note 支持内存碎片整理，会优先使用间隙中的空闲块
 */
static void *mgp_getFreeBlock(mgp_pool_t *pool, const size_t bytes)
{
    assert(pool);

    const size_t actualSize = bytes + (sizeof(unsigned int) * 2); // 缓冲区实际大小
    void *ret = NULL;

    if (pool->allocBlockCnt < 1)
    { // 尚未申请块
        // 检查是否有足够的空间
        if (actualSize >= pool->totalSize)
            return NULL; // 空间不足，返回 NULL

        // 当申请块小于 1, 直接从内存池顶部往下放
        ret = (void *)((uintptr_t)pool->baseAddr + pool->totalSize - actualSize);

        // 额外检查：确保返回的地址在有效范围内
        if ((uintptr_t)ret < (uintptr_t)pool->baseAddr)
            return NULL;

#if MGP_FREE_BLOCK_DEBUG
        DEBUG_PRINT("First block, allocating from top");
#endif
        return ret;
    }

    qsort(pool->ctrlTable, pool->allocBlockCnt, sizeof(*pool->ctrlTable), mgp_ctrlBlockCompar);

    // 此时pool->ctrlTable[pool->allocBlockCnt]并未存在, 但并不妨碍我们给它预留一段空间
    uintptr_t ctrlTableEndAddr = (uintptr_t)(&pool->ctrlTable[pool->allocBlockCnt]) + sizeof(*pool->ctrlTable);

    int idx = 0;
    uintptr_t freeBlockEndAddr = (uintptr_t)pool->baseAddr + pool->totalSize,                       // 指向内存池末端地址
        freeBlockStartAddr = (uintptr_t)pool->ctrlTable[idx].pHead + pool->ctrlTable[idx].buffSize; // 此时已经将控制块表重排, 直接指向高位块的末端地址
    size_t freeBytes = 0U;

    do
    {
        // 寻找合适的空闲块
        freeBytes = freeBlockEndAddr - freeBlockStartAddr;
#if MGP_FREE_BLOCK_DEBUG
        DEBUG_PRINT("free bytes = %u, idx = %d", freeBytes, idx);
#endif
        if (freeBytes >= actualSize)
        {
            ret = (void *)freeBlockStartAddr;
            return ret; // 直接返回该合适的块地址
        }
        idx++;
        // 更新空闲块地址
        freeBlockStartAddr = (uintptr_t)pool->ctrlTable[idx].pHead + pool->ctrlTable[idx].buffSize;
        freeBlockEndAddr = (uintptr_t)pool->ctrlTable[idx - 1].pHead;
    } while (idx < pool->allocBlockCnt);

    // 所有块间隙中没有合适的空闲块，直接返回新的空闲块
    // 但需要先检查是否有足够的空间
    uintptr_t lastBlockStart = (uintptr_t)pool->ctrlTable[pool->allocBlockCnt - 1].pHead;

    // 检查空间是否足够
    if (lastBlockStart < actualSize)
        return NULL; // 空间不足，返回 NULL

    ret = (void *)(lastBlockStart - actualSize);

    // 额外检查：确保返回的地址在有效范围内
    if ((uintptr_t)ret < (uintptr_t)pool->baseAddr)
        return NULL;

    return ret;
}

/**
 * @brief 从内存池分配内存
 * @param[in] poolAddr 内存池句柄
 * @param[in] bytes 需要分配的字节数
 * @return 成功返回内存块指针，失败返回 NULL
 * @details
 * 分配流程：
 * 1. 调用 mgp_getFreeBlock 寻找合适的空闲块
 * 2. 调用 mgp_createWithCtrlBlock 创建并初始化控制块
 * 3. 返回用户可用的缓冲区指针（pBuff）
 *
 * @note 实际分配大小 = bytes + 2*sizeof(unsigned int)（头尾保护标记）
 */
void *mgp_malloc(mgp_t poolAddr, const size_t bytes)
{
#if MGP_MEMORY_ALLOC_DEBUG
    assert(poolAddr);
#endif
    mgp_pool_t *p = poolAddr;

    // 先检查是否超过最大可分配尺寸（包含头尾标记）
    const size_t maxUserSize = mgp_canAllocMaxSize(p);
    if (bytes > maxUserSize)
        return NULL; // 超过最大可分配尺寸

    const size_t actualSize = bytes + (sizeof(unsigned int) * 2); // 缓冲区实际大小

    void *block = mgp_getFreeBlock(p, bytes);
    if (!block)
        return NULL;

    mgp_ctrl_t *pCtrl = mgp_createWithCtrlBlock(p, block, actualSize);
    if (!pCtrl)
        return NULL;

#if MGP_MEMORY_ALLOC_DEBUG
    // 仅在 DEBUG 模式下打印日志
    DEBUG_PRINT("Allocated block at: %p, size: %u", pCtrl->pBuff, (unsigned int)bytes);
#endif

    return pCtrl->pBuff;
}

/**
 * @brief 缓冲区地址比较函数
 * @param[in] a 第一个地址指针
 * @param[in] b 第二个地址指针
 * @return 相等返回 0，不等返回非 0
 * @details 用于查找指定地址的控制块
 */
static int mgp_buffAddrCompare(const void *a, const void *b)
{
    const uintptr_t pa = (uintptr_t)a;
    const uintptr_t pb = (uintptr_t)b;

    return (pa == pb) ? 0 : !0;
}

/**
 * @brief 检查内存块的头部和尾部魔术数
 * @param[in] ctrl 控制块指针
 * @return 返回值说明：
 *         - (-1): 参数错误（ctrl 为 NULL）
 *         - 0: 魔术数校验通过
 *         - 1: 魔术数不匹配（数据已损坏）
 * @details 
 * 验证控制块的头部和尾部保护标记是否完整：
 * - 检查头部标记是否等于 MGP_GUARD_HEAD (0xDEADBEEF)
 * - 检查尾部标记是否等于 MGP_GUARD_TAIL (0xCAFEBABE)
 * - 如果任一标记不匹配，说明内存块已被破坏
 * 
 * @note 该函数用于 mgp_free() 中检测内存块完整性
 * @warning 传入 NULL 指针会返回 -1，不会触发 assert
 */
static int mgp_magicNumHCheck(const mgp_ctrl_t *ctrl)
{
    if (ctrl)
        return -1;

    if (*ctrl->pHead != MGP_GUARD_HEAD ||
        *ctrl->pTail != MGP_GUARD_TAIL)
    {
        return !0;
    }
    return 0;
}

/**
 * @brief 释放已分配的内存块
 * @param[in] poolAddr 内存池句柄
 * @param[in] p 要释放的内存块指针
 * @details
 * 释放流程：
 * 1. 遍历控制块表，查找匹配的控制块
 * 2. 清空找到的控制块
 * 3. 将最后一个控制块前移填充空位（保持数组紧凑）
 * 4. 减少分配计数
 *
 * @note 如果传入的指针不是从该池分配的，会打印错误信息
 */
void mgp_free(mgp_t poolAddr, void *p)
{
    assert(poolAddr);
    assert(p);

    mgp_pool_t *pool = poolAddr;
    mgp_ctrl_t *ctrl = NULL;
    int idx = pool->allocBlockCnt;
    int ret = 0;
    if (idx == 1)
    {
        ret = mgp_buffAddrCompare(pool->ctrlTable[pool->allocBlockCnt - 1].pBuff, p);
        if (ret)
        {
#if MGP_MEMORY_FREE_DEBUG
            ERROR_PRINT("this pos is not alloc.");
#endif
            return;
        }
        ctrl = &pool->ctrlTable[pool->allocBlockCnt - 1];
        goto _free;
    }
    for (idx = 0; idx < pool->allocBlockCnt; idx++)
    {
        ret = mgp_buffAddrCompare(pool->ctrlTable[idx].pBuff, p);
        if (!ret)
        {
            ctrl = &pool->ctrlTable[idx];
            goto _free;
        }
    }
#if MGP_MEMORY_FREE_DEBUG
    ERROR_PRINT("this pos is not alloc.");
#endif
    return;
_free:
    ret = mgp_magicNumHCheck(ctrl);
    switch (ret)
    {
    case -1:
        return;

    default:
        printf("The checksum has been corrupted. Please verify the data integrity.\n");
        break;
    }
    memset(ctrl, 0, sizeof(*ctrl));
    if (pool->allocBlockCnt > 1)
    {
        // 申请内存块数大于 1, 列表末端的控制块往前填充
        memcpy(ctrl, &pool->ctrlTable[pool->allocBlockCnt - 1], sizeof(*ctrl));
    }
    pool->allocBlockCnt -= 1;
    VAR_PRINT_INT(pool->allocBlockCnt);
    if (pool->allocBlockCnt < 0)
    { // 应该永远不会满足该条件
#if MGP_MEMORY_FREE_DEBUG
        ERROR_PRINT("ERROR pool->allocBlockCnt = %d", pool->allocBlockCnt);
#endif
    }
    return;
}

#if MGP_DEBUG

/**
 * @brief 显示所有控制块信息（调试用）
 * @param[in] pool 内存池指针
 * @details 打印所有已分配控制块的头部地址
 */
static void mgp_showCtrlBlocks(mgp_pool_t *pool)
{
    assert(pool);

    for (int i = 0; i < pool->allocBlockCnt; i++)
    {
        DEBUG_PRINT("addr[%d] = %p", i, pool->ctrlTable[i].pHead);
    }
}

/**
 * @brief 控制块测试函数（调试用）
 * @param[in] pool 内存池指针
 * @details
 * 生成随机地址的控制块并测试排序功能
 * @note 仅用于测试目的
 */
static void mgp_ctrlBlockTest(mgp_pool_t *pool)
{
    assert(pool);

    pool->allocBlockCnt = 8;
    srand(0x456);

    for (int i = 0; i < pool->allocBlockCnt; i++)
    {
        pool->ctrlTable[i].pHead = (void *)(rand() % 0x1000);
    }

    mgp_showCtrlBlocks(pool);
    qsort(pool->ctrlTable, pool->allocBlockCnt, sizeof(*pool->ctrlTable), mgp_ctrlBlockCompar);
    mgp_showCtrlBlocks(pool);
}

/**
 * @brief 显示所有已分配的内存块信息
 * @param[in] pool 内存池指针
 * @details
 * 打印每个内存块的：
 * - 索引号
 * - 缓冲区地址（pBuff）
 * - 可用大小（不包含头尾标记）
 */
void mgp_showAllocBlock(const mgp_pool_t *pool)
{
    assert(pool);

    for (int i = 0; i < pool->allocBlockCnt; i++)
    {
        DEBUG_PRINT("block[%d], addr[%x], size[%u]",
                    i, (uintptr_t)pool->ctrlTable[i].pBuff, pool->ctrlTable[i].buffSize - (sizeof(unsigned int) * 2));
    }
}

/**
 * @brief 内存池综合测试函数
 * @param[in] p 内存池句柄
 * @details
 * 执行以下测试：
 * 1. 分配 3 个不同大小的内存块（32、24、16 字节）
 * 2. 向第一块写入数据并验证
 * 3. 释放第一块后重新分配
 * 4. 依次释放所有块
 * 5. 打印各块地址信息
 *
 * @note 仅用于测试目的，生产环境不应调用
 */
void mgp_test(mgp_t p)
{
    assert(p);
    mgp_pool_t *pool = p;
    // mgp_ctrlBlockTest(pool);

    void *mem1, *mem2, *mem3;
    VAR_PRINT_UD(sizeof(unsigned int *));
    mem1 = mgp_malloc(p, 32);
    mem2 = mgp_malloc(p, 24);
    mem3 = mgp_malloc(p, 16);

    mgp_showAllocBlock(p);

    memcpy(mem1, "HELLO WORLD ! ! !", sizeof("HELLO WORLD ! ! !"));
    VAR_PRINT_STRING((char *)mem1);
    VAR_PRINT_ARR_HEX((unsigned char *)mem1, 32);

    mgp_free(p, mem1);
    mgp_showAllocBlock(p);

    mem1 = mgp_malloc(p, 24);
    mgp_showAllocBlock(p);

    mgp_free(p, mem3);
    mgp_showAllocBlock(p);

    mgp_free(p, mem2);
    mgp_showAllocBlock(p);

    mgp_free(p, mem1);

    VAR_PRINT_HEX((uintptr_t)mem1);
    VAR_PRINT_HEX((uintptr_t)mem2);
    VAR_PRINT_HEX((uintptr_t)mem3);
}
#endif
