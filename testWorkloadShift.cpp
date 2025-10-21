#include "testWorkloadShift.h"
#include "printResults.h"
#include "KArcCache.h"
#include <iostream>
#include <string>
#include <array>
#include <random>
#include "LRU_K.h"
#include "LFU.h"
void testWorkloadShift::operator()() {
    std::cout << "\n=== Test scenario 3: Workload drastic changes test ===" << std::endl;

    const int CAPACITY = 30;
    const int OPERATIONS = 80000;
    const int PHASE_LENGTH = OPERATIONS / 5;  // 每个阶段的长度
    KArcCache::KLruCache<int, std::string> lru(CAPACITY);
    KArcCache::KLfuCache<int, std::string> lfu(CAPACITY, 2);
    KArcCache::ArcCache<int, std::string> arc(CAPACITY, 25);

    std::random_device rd;
    std::mt19937 gen(rd());

    std::array<KArcCache::KICachePolicy<int, std::string>*, 3> caches = { &lru, &lfu, &arc };
    std::vector<int> hits(3, 0);
    std::vector<int> get_operations(3, 0);
    std::vector<std::string> names = { "LRU", "LFU", "ARC" };

    // 为每种缓存算法运行相同的测试
    for (int i = 0; i < caches.size(); ++i) {
        // 先预热缓存，只插入少量初始数据
        for (int key = 0; key < 30; key++)
        {
            std::string value = "init" + std::to_string(key); caches[i]->put(key, value);
        }

        for (int op = 0; op < OPERATIONS; op++)
        {
            int phase = op / PHASE_LENGTH; 
            

            int putProbability;
            switch (phase) {
            case 0:putProbability = 15; break;  // 阶段1: 热点访问，15%写入更合理
            case 1: putProbability = 30; break;  // 阶段2: 大范围随机，降低写比例为30%
            case 2: putProbability = 10; break;  // 阶段3: 顺序扫描，10%写入保持不变
            case 3: putProbability = 25; break;  // 阶段4: 局部性随机，微调为25%
            case 4: putProbability = 20; break;  // 阶段5: 混合访问，调整为20%
            default: putProbability = 20;
            }

            // 确定是读还是写操作
            bool isPut = (gen() % 100 < putProbability);
            
            //“op < 16000*2”，也就是“op < 32000”。
            //所以在 0~31999 这段里，phase 是 0 或 1。
            int key;
            if (op < PHASE_LENGTH) {// 阶段1: 热点访问 - 减少热点数量从10到5，使热点更集中
                key = gen() % 5;//只在 0~4 五个热点上循环访问
            }
            else if (op < PHASE_LENGTH * 2) { // 阶段2: 大范围随机 - 范围从1000减小到400，更适合20大小的缓存
                key = gen() % 400;//在 0~399 间随机访问，工作集很大
            }
            else if (op < PHASE_LENGTH * 3) {  // 阶段3: 顺序扫描 - 保持100个键
                key = (op - PHASE_LENGTH * 2) % 100; //从 0~99 顺序访问（扫描型）
            }
            else if (op < PHASE_LENGTH * 4) {  // 阶段4: 局部性随机 - 优化局部性区域大小
                // 产生5个局部区域，每个区域大小为15个键，与缓存大小20接近但略小
                int locality = (op / 800) % 5;  // 调整为5个局部区域
                key = locality * 15 + (gen() % 15);  // 每区域15个键
                //先 800 次在 0–14 随机
                //再 800 次在 15–29 随机
                //再 800 次在 30–44 随机
                //...
                // 再回到 0–14

            }
            else {  // 阶段5: 混合访问 - 增加热点访问比例
                int r = gen() % 100;
                if (r < 40) {  // 40%概率访问热点（从30%增加）
                    key = gen() % 5;  // 5个热点键
                }
                else if (r < 70) {  // 30%概率访问中等范围
                    key = 5 + (gen() % 45);  // 缩小中等范围为50个键
                }
                else {  // 30%概率访问大范围（从40%减少）
                    key = 50 + (gen() % 350);  // 大范围也相应缩小
                }
            }

            if (isPut) {
                // 执行写操作
                std::string value = "value" + std::to_string(key) + "_p" + std::to_string(phase);
                caches[i]->put(key, value);
            }
            else {
                // 执行读操作并记录命中情况
                std::string result;
                get_operations[i]++;
                if (caches[i]->get(key, result)) {
                    hits[i]++;
                }
            }
        }
        printResults("Workload drastic change test", CAPACITY, get_operations[i], hits[i]);
    }

}