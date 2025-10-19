#pragma once
#include <unordered_map>
#include <mutex>
#include "KArcCacheNode.h"
namespace KArcCache {

	template<typename Key, typename Value>
	class ArcLruPart {
	public:
		using NodeType = ArcNode<Key, Value>;
		using NodePtr = std::shared_ptr<NodeType>;
		using NodeMap = std::unordered_map<Key, NodePtr>;

	private:
		size_t capacity_;
		size_t ghostCapacity_;
		size_t transformThreshold_;
		std::mutex mutex_;


		NodeMap mainCache_;
		NodeMap ghostCache_;

		//main list
		NodePtr mainHead_;
		NodePtr mainTail_;

		//ghost list
		NodePtr ghostHead_;
		NodePtr ghostTail_;

	private:
		void initializeLists() {
			mainHead_ = std::make_shared<NodeType>();
			mainTail_ = std::make_shared<NodeType>();
			mainHead_->next_ = mainTail_;
			mainTail_->prev_ = mainHead_;

			ghostHead_ = std::make_shared<NodeType>();
			ghostTail_ = std::make_shared<NodeType>();
			ghostHead_->next_ = ghostTail_;
			ghostTail_->prev_ = ghostHead_;
		}

		bool updateExistingNode(NodePtr node, const Value& value)
		{
			node->setValue(value);
			moveToFront(node);
			return true;
		}

		bool addNewNode(const Key& key, const Value& value)
		{
			if (mainCache_.size() >= capacity_) {
				evictLeastRecent();
			}
			NodePtr newNode = std::make_shared<NodeType>(key, value);
			mainCache_[key] = newNode;
			addToFront(newNode);
			return true;
		}

		bool updateNodeAccess(NodePtr node)
		{
			moveToFront(node);
			node->increaseAccessCount();
			return true;
		}

		void moveToFront(NodePtr node)
		{
			//从当前位置移除
			if (!node->prev_.expired() && node->next_) {
				auto temp = node->prev_.lock();
				temp->next_ = node->next_;
				node->next_->prev_ = node->prev_;
				node->next_ = nullptr; // 清空指针，防止悬垂引用
			}
			addToFront(node);
		}

		void addToFront(NodePtr node)
		{
			mainHead_->prev_.lock()->next_ = node;
			node->prev_ = mainHead_->prev_;
			node->next_ = mainHead_;
			mainHead_->prev_ = node;
		}

		void evictLeastRecent()
		{
			NodePtr leastRecentNode = mainTail_->prev_.lock();
			if (!leastRecentNode || leastRecentNode == mainHead_) return;
			//从主链表移除
			removeFromMain(leastRecentNode);
			//添加到幽灵缓存
			if (ghostCache_.size() >= ghostCapacity_) {
				removeOldestGhost();
			}
			addToGhost(leastRecentNode);
			//从主缓存映射中移除
			mainCache_.erase(leastRecentNode->getKey());
		}

		void removeFromMain(NodePtr node)
		{
			if (!node->prev_.expired() && node->next_) {
				NodePtr temp = node->prev_.lock();
				temp->next_ = node->next_;
				node->next_->prev_ = node->prev_;
				node->next_ = nullptr;//清空指针
			}
		}

		void removeFromGhost(NodePtr node)
		{
			if (!node->prev_.expired() && node->next_) {
				NodePtr temp = node->prev_.lock();
				temp->next_ = node->next_;
				node->next_->prev_ = node->prev_;
				node->next_ = nullptr;//清空指针
			}
		}

		void addToGhost(NodePtr node)
		{
			// 重置节点的访问计数
			node->accessCount_ = 1;

			//添加到幽灵缓存的头部
			node->next_ = ghostHead_->next_;
			node->prev_ = ghostHead_;
			ghostHead_->next_->prev_ = node;
			ghostHead_->next_ = node;

			//添加到幽灵缓存映射
			ghostCache_[node->getKey()] = node;
		}

		void removeOldestGhost()
		{
			NodePtr oldestGhostNode = ghostTail_->prev_.lock();
			if (!oldestGhostNode || oldestGhostNode == ghostHead_) return;
			removeFromGhost(oldestGhostNode);
			ghostCache_.erase(oldestGhostNode->getKey());
		}

	public:
		explicit ArcLruPart(size_t capacity,size_t transformThreshold):
			capacity_(capacity),
			ghostCapacity_(capacity), 
			transformThreshold_(transformThreshold) {
			initializeLists();
		}

		bool get(Key& key, Value& value) {
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = mainCache_.find(key);
			auto ghostIt = ghostCache_.find(key);
			if (it != mainCache_.end()) {
				value = it->second->getValue();
				updateNodeAccess(it->second);
				return true;
			}
			return false;
		}
        
		bool put(Key key, Value value) {
			if (capacity_ == 0) return false;
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = mainCache_.find(key);
			if (it != mainCache_.end()) {
				return updateExistingNode(it->second, value);
			}
			return addNewNode(key, value);
		}

		bool checkGhost(Key key) {
			auto it = ghostCache_.find(key);
			if (it != ghostCache_.end()) {
				removeFromGhost(it->second);
				ghostCache_.erase(it);
				addNewNode(it->second->getKey(), it->second->getValue());
				return true;
			}
			return false;
		}

		void increaseCapacity() { capacity_++; }

		bool decreaseCapacity() {
			if (capacity_ <= 0)return false;
			if (mainCache_.size() == capacity_) {
				evictLeastRecent();
			}
			--capacity_;
			return true;
		}
	};
}