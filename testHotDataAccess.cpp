#include "testHotDataAccess.h"
#include "printResults.h"


void tetestHotDataAccess::operator()() {
    std::cout << "\n=== Test scenario 1: hotspot data access test ===" << std::endl;

    const int CAPACITY = 20; // Cache size
    const int OPERATIONS = 500000; // total operation count
    const int HOT_KEYS = 20; // hot data num
    const int COLD_KEYS = 5000; // cold data num 
    const int TRANSFORM_THRESHOLD = 10;
    KArcCache::KLruKCache<int, std::string> lru(CAPACITY, 10, 2);
    KArcCache::KLfuCache<int, std::string> lfu(CAPACITY, 10);
    KArcCache::ArcCache<int, std::string> arc(CAPACITY, TRANSFORM_THRESHOLD);

    // generate random data
    std::random_device rd;
    std::mt19937 gen(rd());

    std::array<KArcCache::KICachePolicy<int, std::string>*, 3> caches = { &lru, &lfu, &arc };
    std::vector<int> hits(3, 0); // Record the number of cache hits for each of the three strategies
    std::vector<int> get_operations(3, 0); // The total number of cache accesses for each of the three strategy tests
    std::vector<std::string> names = { "LRU", "LFU", "ARC" };

    // Perform the same sequence of operations on all cached objects
    for (int i = 0; i < caches.size(); i++) {
        // Preheat the cache first and insert some data
        for (int key = 0; key < HOT_KEYS; key++) {
            std::string value = "value" + std::to_string(key);
            caches[i]->put(key, value);
        }

        // Alternate put and get operations to simulate real scenarios
        for (int op = 0; op < OPERATIONS; op++) {
            // In most cache systems, read operations are more frequent than write operations.
            // Therefore, set a 30% probability for write operations.
            bool isPut = (gen() % 100 < 30);
            int key;

            // 70% probability of accessing hot data, 30% probability of accessing cold data
            if (gen() % 100 < 70) {
                key = gen() % HOT_KEYS; // hot data
            }
            else {
                key = HOT_KEYS + (gen() % COLD_KEYS); // cold data
            }

            if (isPut) {
                std::string value = "value" + std::to_string(key) + "_v" + std::to_string(op % 100);
                caches[i]->put(key, value);
            }
            else {
                // Execute the get operation and record the hit situation
                std::string result;
                get_operations[i]++;
                if (caches[i]->get(key, result)) {
                    hits[i]++;
                }
            }
        }
        // 打印测试结果
        printResults(names[i], CAPACITY, get_operations[i], hits[i]);
    }
}
