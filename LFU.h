#include <memory>
#include <unordered_map>
#include <mutex>
#include <climits>
#include <cstring>
#include "KICachePolicy.h"
namespace KArcCache {
    template<typename Key, typename Value> class KLfuCache;

    template<typename Key, typename Value>
    class FreqList {
    private:
        struct node {
            int freq_;
            Key key_;
            Value value_;
            std::weak_ptr<node> prev;
            std::shared_ptr<node> next;
            node(const Key& key, const Value& value) : freq_(1), key_(key), value_(value), next(nullptr) {}
            node() : freq_(1), next(nullptr) {}
        };
        using NodePtr = std::shared_ptr<node>;

        NodePtr head_;
        NodePtr tail_;
        int freq_;

    public:
        explicit FreqList(int n) : freq_(n) {
            head_ = std::make_shared<node>();
            tail_ = std::make_shared<node>();
            head_->next = tail_;
            tail_->prev = head_;
        }
        bool isEmpty() const {
            return head_->next == tail_;
        }
        void addNode(const NodePtr& node) {
            if (!node) return;
            node->prev = tail_->prev;
            node->next = tail_;
            auto p = tail_->prev.lock();
            if (p) p->next = node;
            tail_->prev = node;
        }
        void removeNode(const NodePtr& node) {
            if (!node) return;
            if (node->prev.expired() || !node->next) return;
            auto p = node->prev.lock();
            p->next = node->next;
            node->next->prev = p;
            node->next = nullptr;
            node->prev.reset();
        }
        NodePtr getFirstNode() const { return head_->next; }

        friend class KLfuCache<Key, Value>;
        using Node = node;
        using NodePtrAlias = NodePtr;
    };

    template <typename Key, typename Value>
    class KLfuCache : public KICachePolicy<Key, Value> {
    public:
        using List = FreqList<Key, Value>;
        using Node = typename List::Node;
        using NodePtr = std::shared_ptr<Node>;
        using NodeMap = std::unordered_map<Key, NodePtr>;

    private:
        int  capacity_;
        int  minFreq_;
        int  maxAverageNum_;
        long long curTotalNum_;
        int  curAverageNum_;

        std::mutex mutex_;
        NodeMap nodeMap_;
        std::unordered_map<int, List*> freqToFreqList_;

    private:
        void putInternal(const Key& key, const Value& value);
        void getInternal(const NodePtr& node, Value& value);

        void kickOut();

        void removeFromFreqList(const NodePtr& node);
        void addToFreqList(const NodePtr& node);

        void addFreqNum();
        void decreaseFreqNum(int num);
        void handleOverMaxAverageNum();
        void updateMinFreq();

        List* ensureList(int f) {
            auto it = freqToFreqList_.find(f);
            if (it == freqToFreqList_.end()) {
                auto* lst = new List(f);
                freqToFreqList_[f] = lst;
                return lst;
            }
            return it->second;
        }

    public:
        KLfuCache(int capacity, int maxAverageNum)
            : capacity_(capacity),
            minFreq_(INT_MAX),
            maxAverageNum_(maxAverageNum),
            curTotalNum_(0),
            curAverageNum_(0) {}

        ~KLfuCache() override {
            for (auto& kv : freqToFreqList_) delete kv.second;
            freqToFreqList_.clear();
        }

        void put(Key key, Value value) override {
            if (capacity_ == 0) return;
            std::lock_guard<std::mutex> lk(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end()) {
                it->second->value_ = value;
                getInternal(it->second, value);
                return;
            }
            putInternal(key, value);
        }

        bool get(Key key, Value& value) override {
            if (capacity_ == 0) return false;
            std::lock_guard<std::mutex> lk(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end()) {
                getInternal(it->second, value);
                return true;
            }
            return false;
        }

        Value get(Key key) override {
            Value v{};
            get(key, v);
            return v;
        }

        void purge() {
            std::lock_guard<std::mutex> lk(mutex_);
            nodeMap_.clear();
            for (auto& kv : freqToFreqList_) delete kv.second;
            freqToFreqList_.clear();
            minFreq_ = INT_MAX;
            curTotalNum_ = 0;
            curAverageNum_ = 0;
        }
    };

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::putInternal(const Key& key, const Value& value) {
        if (nodeMap_.size() == static_cast<size_t>(capacity_)) {
            kickOut();
        }
        NodePtr newNode = std::make_shared<Node>(key, value);
        nodeMap_[key] = newNode;
        addToFreqList(newNode);
        addFreqNum();
        if (minFreq_ > 1) minFreq_ = 1;
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::getInternal(const NodePtr& node, Value& value) {
        value = node->value_;
        int oldf = node->freq_;
        removeFromFreqList(node);
        node->freq_ = oldf + 1;
        addToFreqList(node);
        if (oldf == minFreq_) {
            auto it = freqToFreqList_.find(oldf);
            if (it != freqToFreqList_.end() && it->second->isEmpty()) {
                minFreq_ = oldf + 1;
            }
        }
        addFreqNum();
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::kickOut() {
        if (nodeMap_.empty()) return;
        List* lst = ensureList(minFreq_);
        auto node = lst->getFirstNode();
        if (!node || node == nullptr || node->next == nullptr) return;
        removeFromFreqList(node);
        nodeMap_.erase(node->key_);
        decreaseFreqNum(node->freq_);
        updateMinFreq();
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::removeFromFreqList(const NodePtr& node) {
        if (!node) return;
        int f = node->freq_;
        auto it = freqToFreqList_.find(f);
        if (it != freqToFreqList_.end()) it->second->removeNode(node);
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::addToFreqList(const NodePtr& node) {
        if (!node) return;
        int f = node->freq_;
        List* lst = ensureList(f);
        lst->addNode(node);
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::addFreqNum() {
        ++curTotalNum_;
        if (nodeMap_.empty()) curAverageNum_ = 0;
        else curAverageNum_ = static_cast<int>(curTotalNum_ / static_cast<long long>(nodeMap_.size()));
        if (curAverageNum_ > maxAverageNum_) handleOverMaxAverageNum();
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::decreaseFreqNum(int num) {
        if (num <= 0) return;
        curTotalNum_ -= num;
        if (curTotalNum_ < 0) curTotalNum_ = 0;
        if (nodeMap_.empty()) curAverageNum_ = 0;
        else curAverageNum_ = static_cast<int>(curTotalNum_ / static_cast<long long>(nodeMap_.size()));
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::handleOverMaxAverageNum() {
        if (nodeMap_.empty()) return;
        int delta = std::max(1, maxAverageNum_ / 2);
        long long total_after = 0;

        for (auto& kv : nodeMap_) {
            auto& node = kv.second;
            if (!node) continue;
            removeFromFreqList(node);
            node->freq_ -= delta;
            if (node->freq_ < 1) node->freq_ = 1;
            addToFreqList(node);
            total_after += node->freq_;
        }
        curTotalNum_ = total_after;
        if (nodeMap_.empty()) curAverageNum_ = 0;
        else curAverageNum_ = static_cast<int>(curTotalNum_ / static_cast<long long>(nodeMap_.size()));
        updateMinFreq();
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::updateMinFreq() {
        int mf = INT_MAX;
        for (const auto& p : freqToFreqList_) {
            if (p.second && !p.second->isEmpty()) {
                if (p.first < mf) mf = p.first;
            }
        }
        if (mf == INT_MAX) mf = 1;
        minFreq_ = mf;
    }
}