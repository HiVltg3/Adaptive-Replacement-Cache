#pragma once
#include "KICachePolicy.h"
#include "KArcCacheNode.h"
#include "KArcLfuPart.h"
#include "KArcLruPart.h"
#include <stdexcept> // 用于 get 未找到时抛出异常

namespace KArcCache
{
	template<typename Key, typename Value>
	class ArcCache :public KICachePolicy<Key, Value> {
	private:
		size_t capacity_;
		size_t transformThreshold_;
		std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart_;
		std::unique_ptr<ArcLruPart<Key, Value>> lruPart_;

		// 检查幽灵缓存，并执行 ARC 容量自适应调整
		bool checkGhostCaches(Key key) {
			bool capacityAdjusted = false;

			// 1. T1 命中 (LRU Ghost) -> 增加 LRU 容量，减少 LFU 容量
			if (lruPart_->checkGhost(key)) {
				if (lfuPart_->decreaseCapacity()) {
					lruPart_->increaseCapacity();
					capacityAdjusted = true;
				}
			}

			// 2. T2 命中 (LFU Ghost) -> 增加 LFU 容量，减少 LRU 容量
			if (lfuPart_->checkGhost(key)) {
				if (lruPart_->decreaseCapacity()) {
					lfuPart_->increaseCapacity();
					capacityAdjusted = true;
				}
			}
			return capacityAdjusted;
		}


	public:
		// 构造函数：将总容量 capacity 平均分配给 LRU 和 LFU 部分
		explicit ArcCache(size_t capacity = 20, size_t transformThreshold = 2) :
			capacity_(capacity),
			transformThreshold_(transformThreshold),
			lfuPart_(std::make_unique <ArcLfuPart<Key, Value>>(capacity / 2, transformThreshold)),
			lruPart_(std::make_unique <ArcLruPart<Key, Value>>(capacity / 2, transformThreshold)) {

		}
		~ArcCache() override = default;

		// 实现 KICachePolicy::put - 插入或更新缓存项
		void put(Key key, Value value) override {
			// 1. 检查并执行 ARC 容量调整（Ghost Cache 命中时）
			checkGhostCaches(key);

			// 2. 执行 put 操作：优先检查 LFU（频率更高），否则交给 LRU
			if (lfuPart_->contain(key)) {
				lfuPart_->put(key, value);
			}
			else {
				// 新节点，或 LRU 部分的节点（包括刚刚从 Ghost 提升上来的节点）
				lruPart_->put(key, value);
			}
		}

		// 实现 KICachePolicy::get (带传出参数) - 查找缓存项
		bool get(Key key, Value& value) override {
			// 1. 尝试从 LRU 部分获取（LRU内部会更新节点位置）
			if (lruPart_->get(key, value)) {
				return true;
			}
			// 2. 尝试从 LFU 部分获取（LFU内部会更新频率）
			if (lfuPart_->get(key, value)) {
				return true;
			}
			return false;
		}

		// 实现 KICachePolicy::get (直接返回值) - 查找缓存项
		Value get(Key key) override {
			Value value;
			if (get(key, value)) {
				return value;
			}
			// 最佳实践：如果未找到，抛出异常
			throw std::out_of_range("Key not found in ArcCache");
		}
	};
};
