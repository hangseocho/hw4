#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <random>
#include <numeric>
#include <cassert>
#include "pool.hpp" 

template <typename T>
inline void do_not_optimize(T&& val) {
    asm volatile("" : "+r,m"(val) : : "memory");
}

class Base {
public:
    virtual ~Base() = default;
    virtual int compute(int x) = 0;
};

class DerivedA : public Base {
    int factor = 3;
public:
    int compute(int x) override { return x * factor; }
};

class DerivedB : public Base {
    int bias = 7;
public:
    int compute(int x) override { return x + bias; }
};

class FinalDerived final : public Base {
    int val = 5;
public:
    int compute(int x) override { return x ^ val; }
};

class DirectClass {
    int factor = 3;
public:
    int compute(int x) { return x * factor; }
};

void test_polymorphism_and_leak() {
    constexpr std::size_t CAPACITY = 10;
    std::size_t max_obj_size = std::max({sizeof(DerivedA), sizeof(DerivedB), sizeof(FinalDerived)});
    Pool pool(max_obj_size, CAPACITY);
    std::vector<Base*> objects;

    for (std::size_t i = 0; i < CAPACITY; ++i) {
        void* mem = pool.alloc();
        assert(mem != nullptr);
        Base* obj = (i % 2 == 0) ? static_cast<Base*>(new (mem) DerivedA()) : static_cast<Base*>(new (mem) DerivedB());
        objects.push_back(obj);
    }
    for (auto* obj : objects) do_not_optimize(obj->compute(10));
    for (auto* obj : objects) {
        obj->~Base();
        pool.free(obj);
    }
    std::cout << "[PASS] Polymorphism & Pool Free Test Completed Successfully.\n";
}

void benchmark_virtual_calls() {
    constexpr std::size_t N = 10000000;
    DirectClass direct_obj;
    DerivedA hot_virtual_obj;
    FinalDerived final_obj;

    std::vector<Base*> predictable_targets(N, &hot_virtual_obj);
    std::vector<Base*> unpredictable_targets(N);

    DerivedA objA; DerivedB objB;
    std::mt19937 rng(42);
    for (std::size_t i = 0; i < N; ++i) {
        unpredictable_targets[i] = (rng() % 2 == 0) ? static_cast<Base*>(&objA) : static_cast<Base*>(&objB);
    }

    std::cout << "\n=== Virtual Call Cost Benchmark (" << N << " calls) ===\n";

    {
        auto start = std::chrono::high_resolution_clock::now();
        int sum = 0;
        for (std::size_t i = 0; i < N; ++i) sum += direct_obj.compute(static_cast<int>(i));
        do_not_optimize(sum);
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "1. Direct Call:              " << std::chrono::duration<double, std::nano>(end - start).count() / N << " ns/call\n";
    }
    {
        auto start = std::chrono::high_resolution_clock::now();
        int sum = 0;
        for (std::size_t i = 0; i < N; ++i) sum += predictable_targets[i]->compute(static_cast<int>(i));
        do_not_optimize(sum);
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "2. Virtual Call (Hot Target): " << std::chrono::duration<double, std::nano>(end - start).count() / N << " ns/call\n";
    }
    {
        auto start = std::chrono::high_resolution_clock::now();
        int sum = 0;
        for (std::size_t i = 0; i < N; ++i) sum += unpredictable_targets[i]->compute(static_cast<int>(i));
        do_not_optimize(sum);
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "3. Virtual Call (Random):     " << std::chrono::duration<double, std::nano>(end - start).count() / N << " ns/call\n";
    }
    {
        Base* final_ptr = &final_obj;
        auto start = std::chrono::high_resolution_clock::now();
        int sum = 0;
        for (std::size_t i = 0; i < N; ++i) sum += final_ptr->compute(static_cast<int>(i));
        do_not_optimize(sum);
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "4. Final Class Call:          " << std::chrono::duration<double, std::nano>(end - start).count() / N << " ns/call\n";
    }
}

void benchmark_allocators() {
    constexpr std::size_t ALLOC_COUNT = 1000000;
    constexpr std::size_t OBJ_SIZE = sizeof(DerivedA);

    std::cout << "\n=== Allocator Performance Benchmark (" << ALLOC_COUNT << " alloc/free) ===\n";

    double t_malloc = 0.0;
    {
        std::vector<void*> ptrs(ALLOC_COUNT);
        auto start = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < ALLOC_COUNT; ++i) ptrs[i] = std::malloc(OBJ_SIZE);
        for (std::size_t i = 0; i < ALLOC_COUNT; ++i) std::free(ptrs[i]);
        auto end = std::chrono::high_resolution_clock::now();
        t_malloc = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "1. malloc / free:  " << t_malloc << " ms\n";
    }
    {
        Pool pool(OBJ_SIZE, ALLOC_COUNT);
        std::vector<void*> ptrs(ALLOC_COUNT);
        auto start = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < ALLOC_COUNT; ++i) ptrs[i] = pool.alloc();
        for (std::size_t i = 0; i < ALLOC_COUNT; ++i) pool.free(ptrs[i]);
        auto end = std::chrono::high_resolution_clock::now();
        double t_pool = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "2. Pool Allocator: " << t_pool << " ms (Speedup: " << (t_malloc / t_pool) << "x)\n";
    }
    {
        Arena arena(OBJ_SIZE * ALLOC_COUNT + 1024);
        auto start = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < ALLOC_COUNT; ++i) {
            void* p = arena.alloc(OBJ_SIZE);
            do_not_optimize(p);
        }
        arena.reset();
        auto end = std::chrono::high_resolution_clock::now();
        double t_arena = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "3. Arena Allocator: " << t_arena << " ms (Speedup: " << (t_malloc / t_arena) << "x)\n";
    }
}

int main() {
    test_polymorphism_and_leak();
    benchmark_virtual_calls();
    benchmark_allocators();
    return 0;
}