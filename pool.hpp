#ifndef POOL_HPP
#define POOL_HPP

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <new>

// 1. Intrusive Free-List Pool Allocator
struct Pool {
    struct Node { Node* next; };

    std::size_t obj_size;
    std::size_t capacity;
    void* buffer;
    Node* free_list;

    explicit Pool(std::size_t obj_size, std::size_t capacity)
        : obj_size(obj_size < sizeof(Node) ? sizeof(Node) : obj_size),
          capacity(capacity),
          buffer(std::malloc((obj_size < sizeof(Node) ? sizeof(Node) : obj_size) * capacity)),
          free_list(nullptr) {
        if (!buffer) return;
        char* ptr = static_cast<char*>(buffer);
        for (std::size_t i = 0; i < capacity; ++i) {
            Node* node = reinterpret_cast<Node*>(ptr + i * this->obj_size);
            node->next = free_list;
            free_list = node;
        }
    }

    ~Pool() { std::free(buffer); }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    void* alloc() {
        if (!free_list) return nullptr;
        Node* node = free_list;
        free_list = free_list->next;
        return static_cast<void*>(node);
    }

    void free(void* p) {
        if (!p) return;
        Node* node = static_cast<Node*>(p);
        node->next = free_list;
        free_list = node;
    }
};

// 2. Bump / Arena Allocator
struct Arena {
    std::size_t capacity;
    std::size_t offset;
    char* buffer;

    explicit Arena(std::size_t capacity)
        : capacity(capacity), offset(0), buffer(static_cast<char*>(std::malloc(capacity))) {}
    ~Arena() { std::free(buffer); }
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* alloc(std::size_t size, std::size_t align = alignof(std::max_align_t)) {
        if (!buffer) return nullptr;
        std::size_t current_addr = reinterpret_cast<std::size_t>(buffer + offset);
        std::size_t padding = (align - (current_addr % align)) % align;
        if (offset + padding + size > capacity) return nullptr;
        offset += padding;
        void* ptr = buffer + offset;
        offset += size;
        return ptr;
    }

    void reset() { offset = 0; }
};

#endif // POOL_HPP