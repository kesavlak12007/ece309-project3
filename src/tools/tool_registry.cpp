// src/tools/tool_registry.cpp — hash table implementation (spec §4.3).

#include "tools/tool_registry.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

ToolRegistry::ToolRegistry()
    : table_(new Node*[32]{}), size_(0), bucket_count_(32) {}

ToolRegistry::~ToolRegistry() {
    for (std::size_t i = 0; i < bucket_count_; ++i) {
        Node* curr = table_[i];
        while (curr != nullptr) {
            Node* next = curr->next;
            delete curr;  // Also destroys the node's owned Tool.
            curr = next;
        }
    }
    delete[] table_;
}

std::size_t ToolRegistry::hash(std::string_view key) noexcept {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(h);
}

Tool* ToolRegistry::find(std::string_view name) const {
    const std::size_t idx = hash(name) % bucket_count_;
    for (Node* curr = table_[idx]; curr != nullptr; curr = curr->next) {
        if (curr->key == name) {
            return curr->tool.get();
        }
    }
    return nullptr;
}

void ToolRegistry::rehash(std::size_t new_bucket_count) {
    // All allocation happens before relinking the existing nodes.
    Node** next_table = new Node*[new_bucket_count]{};
    for (std::size_t i = 0; i < bucket_count_; ++i) {
        Node* curr = table_[i];
        while (curr != nullptr) {
            Node* next = curr->next;
            const std::size_t idx = hash(curr->key) % new_bucket_count;
            curr->next = next_table[idx];
            next_table[idx] = curr;
            curr = next;
        }
    }
    delete[] table_;
    table_ = next_table;
    bucket_count_ = new_bucket_count;
}

bool ToolRegistry::add(std::unique_ptr<Tool> tool) {
    if (!tool) return false;

    // Tool::name() returns a string by value; keep an owned key, not a view.
    std::string name = tool->name();
    if (find(name) != nullptr) return false;

    // Stage the new node so failed allocation cannot change the old table.
    auto node = std::make_unique<Node>(Node{std::move(name), std::move(tool), nullptr});

    // Grow before an insertion would push the load factor above 3/4.
    if (size_ >= bucket_count_ - bucket_count_ / 4) {
        if (bucket_count_ > static_cast<std::size_t>(-1) / 2) {
            throw std::length_error("registry bucket count overflow");
        }
        rehash(bucket_count_ * 2);
    }

    const std::size_t idx = hash(node->key) % bucket_count_;
    node->next = table_[idx];
    table_[idx] = node.release();
    ++size_;
    return true;
}

std::size_t ToolRegistry::size() const noexcept { return size_; }

void ToolRegistry::for_each(const std::function<void(const Tool&)>& fn) const {
    for (std::size_t i = 0; i < bucket_count_; ++i) {
        for (Node* curr = table_[i]; curr != nullptr; curr = curr->next) {
            fn(*curr->tool);
        }
    }
}

std::size_t ToolRegistry::bucket_count() const noexcept { return bucket_count_; }

double ToolRegistry::load_factor() const noexcept {
    return static_cast<double>(size_) / bucket_count_;
}

std::size_t ToolRegistry::max_probe_length() const {
    std::size_t longest = 0;
    for (std::size_t i = 0; i < bucket_count_; ++i) {
        std::size_t length = 0;
        for (Node* curr = table_[i]; curr != nullptr; curr = curr->next) {
            ++length;
        }
        if (length > longest) longest = length;
    }
    return longest;
}
