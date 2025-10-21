#include "testLoopPattern.h"
#include "printResults.h"
#include "KArcCache.h"
#include <iostream>
#include <string>
#include <array>
#include <random>
#include "LRU_K.h"
#include "LFU.h"
void testLoopPattern::operator()() {
    std::cout << "\n=== Test scenario 2: cyclic scanning test ===" << std::endl;

    const int CAPACITY = 50;
    const int LOOP_SIZE = 500;
    const int OPERATIONS = 200000;

    KArcCache::KLruCache<int, std::string> lru(CAPACITY);
    KArcCache::KLfuCache<int, std::string> lfu(CAPACITY,2);
    KArcCache::ArcCache<int, std::string> arc(CAPACITY,25);

    std::array<KArcCache::KICachePolicy<int, std::string>*, 3> caches = { &lru, &lfu, &arc };
    std::vector<int> hits(3, 0);
    std::vector<int> get_operations(3, 0);
    std::vector<std::string> names = { "LRU", "LFU", "ARC" };

    for (int i = 0; i < caches.size(); ++i) {
        // 每轮固定种子，确保三算法看到同一随机序列
        std::mt19937 gen(123456);

        // 预热不超过容量，且不计入统计
        for (int k = 0; k < std::min(CAPACITY, LOOP_SIZE); ++k) {
            caches[i]->put(k, "warm");
        }

        int current_pos = 0;
        int discard_gets = 2 * CAPACITY;   // 丢弃前 2C 次 get
        int seen_gets = 0;

        for (int op = 0; op < OPERATIONS; ++op) {
            // CHANGE 3: 真正 20% 写
            bool isPut = (gen() % 100 < 20);
            int key;

            int m = op % 100;
            if (m < 60) {                  // 60% 顺序
                key = current_pos;
                current_pos = (current_pos + 1) % LOOP_SIZE;
            }
            else if (m < 90) {           // 30% 随机
                key = int(gen() % LOOP_SIZE);
            }
            else {                        // 10% 越界，只读
                key = LOOP_SIZE + int(gen() % LOOP_SIZE);
            }

            // 禁止越界写入，避免污染
            if (key >= LOOP_SIZE) isPut = false;

            if (isPut) {
                std::string value = "loop" + std::to_string(key) + "_v" + std::to_string(op % 100);
                caches[i]->put(key, value);
            }
            else {
                std::string result;
                bool hit = caches[i]->get(key, result);
                //丢弃冷启动 get，不计入统计
                if (seen_gets >= discard_gets) {
                    get_operations[i]++;
                    if (hit) hits[i]++;
                }
                else {
                    seen_gets++;
                }
            }
        }

        printResults(names[i], CAPACITY, get_operations[i], hits[i]);
    }
}
