#pragma once
#include <memory>
#include <unordered_map>
#include <mutex>
#include <stdexcept> // For std::out_of_range
#include "KICachePolicy.h" // 确保包含 KICachePolicy

namespace KArcCache {
	template<typename Key, typename Value> class KLruCache; // 前向声明

	template<typename Key, typename Value>
	class LruNode {
	private:
		Key key_;
		Value value_;
		size_t accessCount_;
		std::shared_ptr<LruNode<Key, Value>> next_;
		std::weak_ptr<LruNode<Key, Value>> prev_;
	public:
		LruNode(Key k, Value v) :key_(k), value_(v), accessCount_(1) {}
		// 默认构造函数用于虚拟节点 (dummy node)
		LruNode() : accessCount_(0) {}
		Key getKey()const { return key_; }
		const Value& getValue()const { return value_; } // 返回 const 引用更高效
		void setValue(const Value& v) { value_ = v; }
		size_t getAccessCount()const { return accessCount_; }
		void increaseAccessCount() { ++accessCount_; }

		friend class KLruCache<Key, Value>;
	};

	template<typename Key, typename Value>
	class KLruCache :public KICachePolicy<Key, Value> { // 继承 KICachePolicy
	public:
		using LruNodeType = LruNode<Key, Value>;
		using NodePtr = std::shared_ptr<LruNodeType>;
		using NodeMap = std::unordered_map<Key, NodePtr>;

	private:
		int capacity_;// 缓存容量
		NodeMap nodeMap_;// key -> Node
		std::mutex mutex_;
		NodePtr dummyHead_;// 虚拟头节点
		NodePtr dummyTail_;// 虚拟尾节点

		void initializeList() {
			// 虚拟节点的 Key 和 Value 可以是默认构造的
			dummyHead_ = std::make_shared<LruNodeType>();
			dummyTail_ = std::make_shared<LruNodeType>();
			dummyHead_->next_ = dummyTail_;
			// 确保 prev_ 是强引用 (shared_ptr) 才能被赋值
			// 注意：这里的 LruNode 定义中 prev_ 是 weak_ptr，如果需要赋强引用，需要先 lock()
			// 但是对于链表初始化，为了避免循环引用，通常 head->next 是强引用，tail->prev 是弱引用
			// 为了简化且符合您的定义，我们确保链表连接逻辑正确
			dummyTail_->prev_ = dummyHead_; // 注意：这里是 weak_ptr = shared_ptr，隐式转换
		}

		void updateExistingNode(NodePtr node, const Value& value) {
			node->setValue(value);
			moveToMostRecent(node);
		}

		void addNewNode(const Key& key, const Value& value) {
			if (nodeMap_.size() >= capacity_) {
				evictLeastRecent();
			}
			NodePtr newNode = std::make_shared<LruNodeType>(key, value);
			insertNode(newNode);
			nodeMap_[key] = newNode;
		}

		// 将该节点移动到最新的位置
		void moveToMostRecent(NodePtr node) {
			// 1. 移除节点
			removeNode(node);
			// 2. 插入到链表头部/尾部（最新访问的位置，这里是 dummyTail 前）
			insertNode(node);
		}

		// 修正：双向链表移除操作
		void removeNode(NodePtr node) {
			if (!node) return;
			// 避免删除虚拟头尾节点
			if (node.get() == dummyHead_.get() || node.get() == dummyTail_.get()) return;

			// 必须先 lock() 获取强引用才能访问 prev 指向的节点的成员
			auto prev_node = node->prev_.lock(); // 获取左侧邻居的强引用
			auto next_node = node->next_;        // 获取右侧邻居的强引用

			if (prev_node) {
				// 左邻居的 next_ 指向右邻居
				prev_node->next_ = next_node;
			}
			if (next_node) {
				// 右邻居的 prev_ 指向左邻居
				next_node->prev_ = prev_node;
			}

			// 清除自身的 next/prev 引用
			node->next_.reset();
			// node->prev_ 会自动释放其管理的 weak_ptr
		}

		void insertNode(NodePtr node) {
			// 新节点插入到 dummyTail 之前 (最新位置)
			NodePtr last_node = dummyTail_->prev_.lock(); // 获取当前最末尾节点的强引用

			// 1. 连接新节点
			node->next_ = dummyTail_;
			node->prev_ = last_node;

			// 2. 更新相邻节点
			if (last_node) last_node->next_ = node;
			dummyTail_->prev_ = node;
		}

		void evictLeastRecent() {
			// 待驱逐节点：dummyHead->next_ (最久未使用)
			NodePtr leastRecent = dummyHead_->next_;
			// 确保不是 dummyTail (空列表的判断)
			if (leastRecent.get() == dummyTail_.get()) return;

			removeNode(leastRecent);
			nodeMap_.erase(leastRecent->getKey());
		}

	public:
		KLruCache(int capacity) :capacity_(capacity) {
			initializeList();
		}

		~KLruCache() override = default;

		// KICachePolicy::put (纯虚函数实现)
		void put(Key key, Value value) override {
			if (capacity_ <= 0) return;
			std::lock_guard<std::mutex> lk(mutex_);
			auto it = nodeMap_.find(key);
			if (it != nodeMap_.end()) {
				updateExistingNode(it->second, value);
				return;
			}
			addNewNode(key, value);
		}

		// KICachePolicy::get (带传出参数，纯虚函数实现)
		bool get(Key key, Value& value) override {
			std::lock_guard<std::mutex> lk(mutex_);
			auto it = nodeMap_.find(key);
			if (it != nodeMap_.end()) {
				moveToMostRecent(it->second);
				value = it->second->getValue();
				return true;
			}
			return false;
		}

		// KICachePolicy::get (直接返回值，纯虚函数实现)
		Value get(Key key) override { // <-- 实现缺失的纯虚函数
			Value value{}; // 安全默认构造
			if (get(key, value)) {
				return value;
			}
			// 更好的 C++ 实践是抛出异常而不是返回默认值
			throw std::out_of_range("Key not found in KLruCache");
		}

		void remove(Key key) {
			std::lock_guard<std::mutex> lk(mutex_);
			auto it = nodeMap_.find(key);
			if (it != nodeMap_.end()) {
				removeNode(it->second);
				nodeMap_.erase(it);
			}
		}
	};

	// LRU优化：Lru-k版本。
	template<typename Key, typename Value>
	class KLruKCache :public KLruCache<Key, Value> {
	private:
		int k_;
		// historyList 只需要记录 Key -> 访问次数，因此历史缓存使用 KLruCache<Key, size_t>
		std::unique_ptr<KLruCache<Key, size_t>> historyList_;
		// historyValueMap 用于临时存储未进入主缓存的值
		std::unordered_map<Key, Value> historyValueMap_;

	public:
		// 修正：基类构造函数调用
		KLruKCache(int capacity, int historyCapacity, int k) :
			// 必须使用基类的构造函数 KLruCache<Key, Value>
			KLruCache<Key, Value>(capacity),
			historyList_(std::make_unique < KLruCache<Key, size_t>>(historyCapacity)),
			k_(k) {}

		KLruKCache() = delete;

		// KICachePolicy::get (直接返回值)
		Value get(Key key) override {
			// 1. 查看是否存在主缓存中
			Value value{};
			// 调用基类的 get(key, value) 来检查和更新主缓存
			bool isInMainCache = KLruCache<Key, Value>::get(key, value);

			// 2. 获取并更新访问历史计数
			size_t historyCount = 0;
			try {
				// 获取历史计数。如果不存在，get() 会抛出异常，捕获后设置为 0
				historyCount = historyList_->get(key);
			}
			catch (const std::out_of_range& e) {
				// Key 不在 historyList 中，historyCount 仍为 0
			}

			historyCount++;
			historyList_->put(key, historyCount);

			// 3. 如果数据在主缓存中，直接返回
			if (isInMainCache) return value;

			// 4. 如果数据不在主缓存，但访问次数达到了k次
			if (historyCount >= k_) {
				// 检查是否有历史值记录 (值只有在 put 时才会记录到 historyValueMap)
				auto it = historyValueMap_.find(key);
				if (it != historyValueMap_.end()) {
					Value storedValue = it->second;
					// 从历史记录移除 (Key 已经达到 k 次，应该晋升)
					historyList_->remove(key);
					historyValueMap_.erase(key);

					// 添加到主缓存 (调用基类 put)
					KLruCache<Key, Value>::put(key, storedValue);

					return storedValue;
				}
				// 访问次数达到 k 次，但 historyValueMap 没有值（这意味着这个 Key 是通过 get 访问达到 k 次的，
				// 但没有 put 进来过，因此无法添加到缓存，返回默认值
			}

			// 5. 数据不在主缓存且不满足添加条件，返回默认值
			return value;
		}

		// KICachePolicy::put
		void put(Key key, Value value) override {
			Value existingValue{};

			// 1. 查看是否存在缓存中，如果存在则更新并返回
			// 调用基类的 get(key, value) 来检查和更新主缓存（将其移动到最新位置）
			bool isInMainCache = KLruCache<Key, Value>::get(key, existingValue);

			if (isInMainCache) {
				// 存在则更新主缓存的值（基类的 put 会调用 updateExistingNode）
				KLruCache<Key, Value>::put(key, value);
				return;
			}

			// 2. 不在主缓存: 获取并更新访问历史
			size_t historyCount = 0;
			try {
				historyCount = historyList_->get(key);
			}
			catch (const std::out_of_range& e) {
				// Key 不在 historyList 中，historyCount 仍为 0
			}

			historyCount++;
			historyList_->put(key, historyCount);

			// 3. 保存/更新值到历史记录映射，供后续get操作使用
			historyValueMap_[key] = value;

			// 4. 检查是否达到k次访问阈值，若达到则晋升
			if (historyCount >= k_) {
				// 达到阈值，晋升到主缓存
				historyList_->remove(key);
				historyValueMap_.erase(key);
				KLruCache<Key, Value>::put(key, value);
			}
		}

		// KICachePolicy::get (带传出参数) - 必须实现
		bool get(Key key, Value& value) override {
			// 对于 LRU-K，我们只需要调用基类的 get 来检查主缓存
			// 历史记录更新应该在 Value get(Key key) 中集中处理，以避免重复逻辑
			// 但为了满足 KICachePolicy 接口，这里只实现主缓存的查找和更新
			return KLruCache<Key, Value>::get(key, value);
		}
	};
}
