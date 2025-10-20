#pragma once
#include "KArcCacheNode.h"
#include <unordered_map>
#include <list>
#include <mutex>
namespace KArcCache
{
	template<typename Key, typename Value>
	class ArcLfuPart {
	public:
		using NodeType = ArcNode<Key, Value>;
		using NodePtr = std::shared_ptr<NodeType>;
		using NodeMap = std::unordered_map<Key, NodePtr>;
		using FreqMap = std::unordered_map<size_t, std::list<NodePtr>>;


	private:
		size_t capacity_;
		size_t ghostCapacity_;
		size_t transformThreshold_;
		size_t minFreq_;
		std::mutex mutex_;

		NodeMap mainCache_;
		NodeMap ghostCache_;
		FreqMap freqMap_;

		NodePtr ghostHead_;
		NodePtr ghostTail_;

		void initializeLists() {
			ghostHead_ = std::make_shared<NodeType>();
			ghostTail_ = std::make_shared<NodeType>();
			ghostHead_->next_ = ghostTail_;
			ghostTail_->prev_ = ghostHead_;
		}

        bool updateExistingNode(NodePtr node, const Value& value)
        {
			node->setValue(value);
			updateNodeFrequency(node);
			return true;
        }

        bool addNewNode(const Key& key, const Value& value)
        {
			if (mainCache_.size() >= capacity_) {
				evictLeastFrequent();
			}
			NodePtr newNode = std::make_shared<NodeType>(key, value);
			newNode->accessCount_ = 1;
			//频次初始化与提升不一致，导致同一节点在两个桶里  
			//如果把新节点丢到 `freqMap_[1]`，但节点自身 `accessCount_` 可能为 0。`updateNodeFrequency` 读到 `oldFreq=0`，会从 `freqMap_[0]` 删（其实没有），
			// 再把节点又放进 `freqMap_[1]`，于是一个节点在 `freqMap_[1]` 出现两次，随后逐出与遍历容易触发容器断言。
			mainCache_[key] = newNode;
			if (freqMap_.find(1) == freqMap_.end()) {
				freqMap_[1] = std::list<NodePtr>();
			}
			freqMap_[1].push_back(newNode);
			minFreq_ = 1;
			return true;
        }

        void updateNodeFrequency(NodePtr node)
        {
			size_t oldFreq = node->getAccessCount();
			if (oldFreq) {// 只有存在的桶才尝试移除
				auto itOld = freqMap_.find(oldFreq);
				if (itOld != freqMap_.end()) {
					itOld->second.remove(node);
					if (itOld->second.empty()) {
						freqMap_.erase(itOld);
						if (minFreq_ == oldFreq) {// 重新计算最小频次
							if (freqMap_.empty()) minFreq_ = 0;
							else {
								size_t m = SIZE_MAX;
								for (auto& kv : freqMap_) m = std::min(m, kv.first);
								minFreq_ = m;
							}
						}
					}
				}
			}
			node->increaseAccessCount();
			size_t newFreq = node->getAccessCount();

			// 添加到新频率列表
			if (freqMap_.find(newFreq) == freqMap_.end()) {
				freqMap_[newFreq] = std::list<NodePtr>();
			}
			freqMap_[newFreq].push_back(node);
			if (!minFreq_ || newFreq < minFreq_) minFreq_ = newFreq;
        }

		void evictLeastFrequent()
		{
			if (freqMap_.empty()) return;

			auto recomputeMin = [&]() -> size_t {
				size_t m = SIZE_MAX;
				for (auto& kv : freqMap_) m = std::min(m, kv.first);
				return (m == SIZE_MAX) ? 0 : m;
				};

			// 确保 minFreq_ 指向存在且非空的桶
			auto fit = freqMap_.find(minFreq_);
			if (minFreq_ == 0 || fit == freqMap_.end() || fit->second.empty()) {
				minFreq_ = recomputeMin();
				if (minFreq_ == 0) return; // 没有可逐出的桶
				fit = freqMap_.find(minFreq_);
				if (fit == freqMap_.end() || fit->second.empty()) return;
			}

			// 从最小频次桶尾部逐出
			auto& listRef = fit->second;
			NodePtr victim = listRef.back();
			listRef.pop_back();

			// 桶空则删除，并重新计算 minFreq_
			if (listRef.empty()) {
				freqMap_.erase(fit);
				minFreq_ = freqMap_.empty() ? 0 : recomputeMin();
			}

			// 从主表删除
			if (victim) {
				mainCache_.erase(victim->getKey());
			}
		}


        void removeFromGhost(NodePtr node){
			if (!node->prev_.expired() && node->next_) {
				auto prev = node->prev_.lock();
				prev->next_ = node->next_;
				node->next_->prev_ = node->prev_;
				node->next_ = nullptr;
			}
        }

        void addToGhost(NodePtr node)
        {
			node->next_ = ghostTail_;
			node->prev_ = ghostTail_->prev_;
			if (!ghostTail_->prev_.expired()) {
				ghostTail_->prev_.lock()->next_ = node;
			}
			ghostTail_->prev_ = node;
			ghostCache_[node->getKey()] = node;
        }

        void removeOldestGhost()
        {
			NodePtr oldestGhostNode = ghostHead_->next_;
			if (oldestGhostNode != ghostTail_) {
				removeFromGhost(oldestGhostNode);
				ghostCache_.erase(oldestGhostNode->getKey());
			}
        }
	public:
		explicit ArcLfuPart(size_t capacity, size_t transformThreshold) :
			capacity_(capacity),
			ghostCapacity_(capacity),
			transformThreshold_(transformThreshold),
			minFreq_(0) {
			initializeLists();
		}

		void put(Key key, Value value)
		{
			if (capacity_ == 0) return;
			std::lock_guard<std::mutex> lk(mutex_);
			auto it = mainCache_.find(key);
			if (it != mainCache_.end()) {
				updateExistingNode(it->second, value);
				return;
			}
			// 未命中：按容量淘汰后新建，频次初始化为 1
			addNewNode(key, value);

		}

		bool get(Key key, Value& value)
		{
			std::lock_guard<std::mutex> lk(mutex_);
			auto it = mainCache_.find(key);
			if (it != mainCache_.end()) {
				value = it->second->getValue();
				updateNodeFrequency(it->second);
				return true;
			}
			return false;
		}

		Value get(Key key) {
			Value value;
			get(key, value);
			return value;
		}

		bool contain(Key key)
		{
			return mainCache_.find(key) != mainCache_.end();
		}

		bool checkGhost(Key key)
		{
			std::lock_guard<std::mutex> lk(mutex_);
			auto it = ghostCache_.find(key);
			if (it != ghostCache_.end()) {
				NodePtr node = it->second;
				removeFromGhost(it->second);
				ghostCache_.erase(it);
				addNewNode(node->getKey(), node->getValue());
				return true;
			}
			return false;
		}

		void increaseCapacity() { ++capacity_; }

		bool decreaseCapacity()
		{
			if (capacity_ <= 0) return false;
			if (mainCache_.size() == capacity_)
			{
				evictLeastFrequent();
			}
			--capacity_;
			return true;
		}
	};
}