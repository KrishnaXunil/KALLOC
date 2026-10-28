#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>
#include <new>
#include <thread>
#include <vector>

namespace {
constexpr std::size_t kDefaultAlignment = alignof(std::max_align_t);

bool IsPowerOfTwo(std::size_t x) { return x != 0 && (x & (x - 1)) == 0; }

std::uintptr_t AlignUp(std::uintptr_t p, std::size_t alignment) {
    const auto mask = static_cast<std::uintptr_t>(alignment - 1);
    return (p + mask) & ~mask;
}

void* MapMemory(std::size_t bytes) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    return p;
}

void UnmapMemory(void* p, std::size_t bytes) {
    if (p) munmap(p, bytes);
}
}

class Allocator {
protected:
    void* m_start_ptr = nullptr;
    std::size_t m_total_size = 0;

    explicit Allocator(std::size_t bytes) : m_total_size(bytes) {
        if (bytes == 0) throw std::invalid_argument("allocator size must be non-zero");
        m_start_ptr = MapMemory(bytes);
        if (!m_start_ptr) throw std::bad_alloc();
    }

public:
    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;
    virtual ~Allocator() { UnmapMemory(m_start_ptr, m_total_size); }

    virtual void* Allocate(std::size_t size, std::size_t alignment = kDefaultAlignment) = 0;
    virtual void Free(void* p) = 0;
    virtual void PrintMemoryMap() const = 0;
};

// Variable-sized first-fit allocator. Physical-neighbor links are separate from
// free-list links, allowing reliable splitting and coalescing.
class FreeListAllocator final : public Allocator {
    struct alignas(std::max_align_t) Block {
        std::size_t size;       // Total bytes in this physical block, including header.
        bool is_free;
        Block* prev_phys;
        Block* next_phys;
        Block* prev_free;
        Block* next_free;
    };

    mutable std::mutex m_mutex;
    Block* m_first = nullptr;
    Block* m_free_head = nullptr;

    static constexpr std::size_t HeaderSize() { return sizeof(Block); }

    void RemoveFree(Block* b) {
        if (b->prev_free) b->prev_free->next_free = b->next_free;
        else m_free_head = b->next_free;
        if (b->next_free) b->next_free->prev_free = b->prev_free;
        b->prev_free = b->next_free = nullptr;
    }

    void InsertFree(Block* b) {
        b->is_free = true;
        b->prev_free = nullptr;
        b->next_free = m_free_head;
        if (m_free_head) m_free_head->prev_free = b;
        m_free_head = b;
    }

    void Coalesce(Block*& b) {
        if (b->prev_phys && b->prev_phys->is_free) {
            Block* prev = b->prev_phys;
            RemoveFree(prev);
            prev->size += b->size;
            prev->next_phys = b->next_phys;
            if (b->next_phys) b->next_phys->prev_phys = prev;
            b = prev;
        }
        if (b->next_phys && b->next_phys->is_free) {
            Block* next = b->next_phys;
            RemoveFree(next);
            b->size += next->size;
            b->next_phys = next->next_phys;
            if (b->next_phys) b->next_phys->prev_phys = b;
        }
    }

public:
    explicit FreeListAllocator(std::size_t bytes) : Allocator(bytes) {
        if (bytes < sizeof(Block) + sizeof(Block*) + 1)
            throw std::invalid_argument("free-list region is too small");
        m_first = reinterpret_cast<Block*>(m_start_ptr);
        m_first->size = bytes;
        m_first->is_free = true;
        m_first->prev_phys = m_first->next_phys = nullptr;
        m_first->prev_free = m_first->next_free = nullptr;
        m_free_head = m_first;
    }

    void* Allocate(std::size_t size, std::size_t alignment = kDefaultAlignment) override {
        if (size == 0 || !IsPowerOfTwo(alignment)) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        for (Block* b = m_free_head; b; b = b->next_free) {
            const auto block_start = reinterpret_cast<std::uintptr_t>(b);
            const auto user_addr = AlignUp(block_start + sizeof(Block) + sizeof(Block*), alignment);
            const auto block_end = block_start + b->size;
            if (user_addr > block_end || size > block_end - user_addr) continue;

            const std::size_t used = static_cast<std::size_t>(
                AlignUp(user_addr + size, alignof(Block)) - block_start);
            const std::size_t min_remainder = sizeof(Block) + sizeof(Block*) + 1;
            RemoveFree(b);

            if (b->size - used >= min_remainder) {
                auto* remainder = reinterpret_cast<Block*>(block_start + used);
                remainder->size = b->size - used;
                remainder->is_free = true;
                remainder->prev_phys = b;
                remainder->next_phys = b->next_phys;
                remainder->prev_free = remainder->next_free = nullptr;
                if (b->next_phys) b->next_phys->prev_phys = remainder;
                b->next_phys = remainder;
                b->size = used;
                InsertFree(remainder);
            }

            b->is_free = false;
            auto** backref = reinterpret_cast<Block**>(user_addr - sizeof(Block*));
            *backref = b;
            return reinterpret_cast<void*>(user_addr);
        }
        return nullptr;
    }

    void Free(void* p) override {
        if (!p) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto addr = reinterpret_cast<std::uintptr_t>(p);
        const auto start = reinterpret_cast<std::uintptr_t>(m_start_ptr);
        if (addr < start + sizeof(Block*) || addr >= start + m_total_size) return;
        Block* b = *(reinterpret_cast<Block**>(addr - sizeof(Block*)));
        if (!b || reinterpret_cast<std::uintptr_t>(b) < start ||
            reinterpret_cast<std::uintptr_t>(b) >= start + m_total_size || b->is_free) return;
        // Coalesce with already-free physical neighbors, then insert the final merged block.
        b->is_free = true;
        Coalesce(b);
        InsertFree(b);
    }

    void PrintMemoryMap() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::cout << "\n[FreeListAllocator map]\n";
        const auto end = reinterpret_cast<std::uintptr_t>(m_start_ptr) + m_total_size;
        auto* b = m_first;
        std::size_t index = 0;
        while (b && reinterpret_cast<std::uintptr_t>(b) < end) {
            std::cout << "Block " << index++ << " | bytes=" << b->size
                      << " | " << (b->is_free ? "FREE" : "ALLOCATED") << '\n';
            b = b->next_phys;
        }
    }
};

// Fixed-size pool. chunk_payload is the maximum requested object size; stride
// includes any bytes needed for the in-chunk next pointer and alignment.
class PoolAllocator final : public Allocator {
    struct Chunk { Chunk* next; };
    mutable std::mutex m_mutex;
    std::size_t m_payload_size;
    std::size_t m_stride;
    std::size_t m_count;
    Chunk* m_free_head = nullptr;
    std::vector<bool> m_allocated;

public:
    PoolAllocator(std::size_t chunk_payload, std::size_t bytes)
        : Allocator(bytes), m_payload_size(chunk_payload),
          m_stride(static_cast<std::size_t>(AlignUp(
              std::max(chunk_payload, sizeof(Chunk)), kDefaultAlignment))),
          m_count(bytes / m_stride), m_allocated(m_count, false) {
        if (chunk_payload == 0 || m_count == 0)
            throw std::invalid_argument("pool size/chunk size is invalid");
        auto* base = static_cast<std::byte*>(m_start_ptr);
        for (std::size_t i = 0; i < m_count; ++i) {
            auto* c = reinterpret_cast<Chunk*>(base + i * m_stride);
            c->next = m_free_head;
            m_free_head = c;
        }
    }

    void* Allocate(std::size_t size, std::size_t alignment = kDefaultAlignment) override {
        if (size == 0 || size > m_payload_size || !IsPowerOfTwo(alignment) || alignment > kDefaultAlignment)
            return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_free_head) return nullptr;
        Chunk* c = m_free_head;
        m_free_head = c->next;
        const auto offset = reinterpret_cast<std::byte*>(c) - static_cast<std::byte*>(m_start_ptr);
        m_allocated[static_cast<std::size_t>(offset) / m_stride] = true;
        return c;
    }

    void Free(void* p) override {
        if (!p) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto start = reinterpret_cast<std::uintptr_t>(m_start_ptr);
        const auto addr = reinterpret_cast<std::uintptr_t>(p);
        if (addr < start || addr >= start + m_count * m_stride) return;
        const auto offset = static_cast<std::size_t>(addr - start);
        if (offset % m_stride != 0) return;
        const auto index = offset / m_stride;
        if (index >= m_count || !m_allocated[index]) return; // reject invalid/double free
        m_allocated[index] = false;
        auto* c = reinterpret_cast<Chunk*>(p);
        c->next = m_free_head;
        m_free_head = c;
    }

    void PrintMemoryMap() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Allocation state is tracked explicitly so map generation is O(number of chunks).
        std::cout << "\n[PoolAllocator map] # = allocated, . = free\n";
        for (std::size_t i = 0; i < m_count; ++i) {
            std::cout << (m_allocated[i] ? "[ # ] " : "[ . ] ");
            if ((i + 1) % 10 == 0) std::cout << '\n';
        }
        std::cout << "\nChunks=" << m_count << ", stride=" << m_stride
                  << ", payload capacity=" << m_payload_size << '\n';
    }
};

// Bump/linear allocator: Free() is intentionally a no-op; Reset() releases all
// allocations in one batch. This makes its benchmark workload different.
class LinearAllocator final : public Allocator {
    mutable std::mutex m_mutex;
    std::size_t m_offset = 0;

public:
    explicit LinearAllocator(std::size_t bytes) : Allocator(bytes) {}

    void* Allocate(std::size_t size, std::size_t alignment = kDefaultAlignment) override {
        if (size == 0 || !IsPowerOfTwo(alignment)) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto base = reinterpret_cast<std::uintptr_t>(m_start_ptr);
        const auto current = base + m_offset;
        const auto aligned = AlignUp(current, alignment);
        const auto padding = static_cast<std::size_t>(aligned - current);
        if (padding > m_total_size - m_offset || size > m_total_size - m_offset - padding) return nullptr;
        m_offset += padding + size;
        return reinterpret_cast<void*>(aligned);
    }

    void Free(void*) override { /* Individual frees are unsupported by design. */ }

    void Reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_offset = 0;
    }

    void PrintMemoryMap() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        const double pct = m_total_size ? 100.0 * static_cast<double>(m_offset) / m_total_size : 0.0;
        std::cout << "\n[LinearAllocator map] offset=" << m_offset << '/' << m_total_size
                  << " bytes used (" << std::fixed << std::setprecision(1) << pct << "%)\n";
    }
};

using Clock = std::chrono::steady_clock;
struct Result { const char* name; double ms; std::size_t ops; };

void PrintResult(const Result& r) {
    const double ns_per_op = r.ops ? r.ms * 1'000'000.0 / static_cast<double>(r.ops) : 0.0;
    const double ops_per_sec = r.ms > 0.0 ? static_cast<double>(r.ops) / (r.ms / 1000.0) : 0.0;
    std::cout << std::left << std::setw(36) << r.name
              << std::right << std::setw(10) << std::fixed << std::setprecision(3) << r.ms << " ms | "
              << std::setw(10) << std::setprecision(1) << ns_per_op << " ns/op | "
              << std::setw(13) << std::setprecision(0) << ops_per_sec << " ops/s\n";
}

Result BenchMalloc(std::size_t iterations, std::size_t bytes) {
    std::vector<void*> ptrs;
    ptrs.reserve(iterations);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        void* p = std::malloc(bytes);
        if (!p) { for (void* q : ptrs) std::free(q); throw std::bad_alloc(); }
        ptrs.push_back(p);
    }
    for (void* p : ptrs) std::free(p);
    const auto end = Clock::now();
    return {"std::malloc + std::free", std::chrono::duration<double, std::milli>(end - begin).count(), iterations * 2};
}

Result BenchFreeList(std::size_t iterations, std::size_t bytes) {
    FreeListAllocator a(std::max<std::size_t>(64 * 1024 * 1024, iterations * (bytes + 128)));
    std::vector<void*> ptrs;
    ptrs.reserve(iterations);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        void* p = a.Allocate(bytes, kDefaultAlignment);
        if (!p) throw std::runtime_error("FreeListAllocator exhausted during benchmark");
        ptrs.push_back(p);
    }
    for (void* p : ptrs) a.Free(p);
    const auto end = Clock::now();
    return {"FreeListAllocator alloc + free", std::chrono::duration<double, std::milli>(end - begin).count(), iterations * 2};
}

Result BenchPool(std::size_t iterations, std::size_t bytes) {
    const std::size_t payload = std::max<std::size_t>(bytes, 128);
    PoolAllocator a(payload, (iterations + 16) * (payload + 32));
    std::vector<void*> ptrs;
    ptrs.reserve(iterations);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        void* p = a.Allocate(bytes, kDefaultAlignment);
        if (!p) throw std::runtime_error("PoolAllocator exhausted during benchmark");
        ptrs.push_back(p);
    }
    for (void* p : ptrs) a.Free(p);
    const auto end = Clock::now();
    return {"PoolAllocator alloc + free", std::chrono::duration<double, std::milli>(end - begin).count(), iterations * 2};
}

Result BenchLinear(std::size_t iterations, std::size_t bytes) {
    // Each batch allocates N objects, then resets once. Compare against malloc/free
    // with the same batch lifetime rather than pretending LinearAllocator supports Free().
    const std::size_t batch_count = 100;
    const std::size_t per_batch = std::max<std::size_t>(1, iterations / batch_count);
    const std::size_t actual = per_batch * batch_count;
    const std::size_t region_size = actual * (bytes + kDefaultAlignment) + 64;
    LinearAllocator a(region_size);
    const auto begin = Clock::now();
    for (std::size_t batch = 0; batch < batch_count; ++batch) {
        for (std::size_t i = 0; i < per_batch; ++i) {
            if (!a.Allocate(bytes, kDefaultAlignment)) throw std::runtime_error("LinearAllocator exhausted during benchmark");
        }
        a.Reset();
    }
    const auto end = Clock::now();
    return {"LinearAllocator alloc + batch reset", std::chrono::duration<double, std::milli>(end - begin).count(), actual + batch_count};
}

// Concurrent stress/throughput test: each thread repeatedly allocates then frees
// one object. Each allocator serializes operations with its own mutex.
template <typename A>
Result BenchConcurrent(const char* name, A& allocator, std::size_t threads, std::size_t iters_per_thread, std::size_t bytes) {
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    workers.reserve(threads);
    const auto begin = Clock::now();
    for (std::size_t t = 0; t < threads; ++t) {
        workers.emplace_back([&allocator, &failed, iters_per_thread, bytes]() {
            for (std::size_t i = 0; i < iters_per_thread; ++i) {
                void* p = allocator.Allocate(bytes, kDefaultAlignment);
                if (!p) { failed.store(true, std::memory_order_relaxed); return; }
                allocator.Free(p);
            }
        });
    }
    for (auto& th : workers) th.join();
    const auto end = Clock::now();
    if (failed.load()) throw std::runtime_error(std::string(name) + " exhausted/failed in concurrent benchmark");
    return {name, std::chrono::duration<double, std::milli>(end - begin).count(), threads * iters_per_thread * 2};
}

int main() {
    try {
        constexpr std::size_t kIterations = 100'000;
        constexpr std::size_t kObjectSize = 64;
        std::cout << "Allocator benchmark (C++20)\n"
                  << "Object size: " << kObjectSize << " bytes; iterations: " << kIterations << "\n"
                  << "All timings include mutex costs for custom allocators.\n\n";

        std::cout << "=== Single-thread throughput ===\n";
        PrintResult(BenchMalloc(kIterations, kObjectSize));
        PrintResult(BenchFreeList(kIterations, kObjectSize));
        PrintResult(BenchPool(kIterations, kObjectSize));
        PrintResult(BenchLinear(kIterations, kObjectSize));
        std::cout << "\nNote: linear allocation uses batch Reset(), not individual Free(); its result is not a strict apples-to-apples comparison.\n";

        constexpr std::size_t kThreads = 8;
        constexpr std::size_t kConcurrentIterations = 20'000;
        std::cout << "\n=== Concurrent custom-allocator throughput (" << kThreads << " threads) ===\n";
        {
            FreeListAllocator a(64 * 1024 * 1024);
            PrintResult(BenchConcurrent("FreeListAllocator concurrent", a, kThreads, kConcurrentIterations, kObjectSize));
        }
        {
            PoolAllocator a(128, (kThreads * kConcurrentIterations + 16) * 160);
            PrintResult(BenchConcurrent("PoolAllocator concurrent", a, kThreads, kConcurrentIterations, kObjectSize));
        }
        {
            // A linear allocator cannot free each object. Each thread uses a private
            // linear allocator to avoid shared-reset lifetime conflicts; this measures
            // concurrent independent arenas, not contention on one shared arena.
            const std::size_t per_thread_bytes = kConcurrentIterations * (kObjectSize + kDefaultAlignment) + 64;
            std::vector<std::unique_ptr<LinearAllocator>> arenas;
            arenas.reserve(kThreads);
            for (std::size_t i = 0; i < kThreads; ++i) arenas.emplace_back(new LinearAllocator(per_thread_bytes));
            std::atomic<bool> failed{false};
            std::vector<std::thread> workers;
            const auto begin = Clock::now();
            for (std::size_t t = 0; t < kThreads; ++t) {
                workers.emplace_back([&, t]() {
                    auto& a = *arenas[t];
                    for (std::size_t i = 0; i < kConcurrentIterations; ++i) {
                        if (!a.Allocate(kObjectSize, kDefaultAlignment)) { failed = true; return; }
                    }
                    a.Reset();
                });
            }
            for (auto& th : workers) th.join();
            const auto end = Clock::now();
            if (failed) throw std::runtime_error("LinearAllocator failed in concurrent benchmark");
            PrintResult({"LinearAllocator concurrent arenas", std::chrono::duration<double, std::milli>(end - begin).count(), kThreads * kConcurrentIterations + kThreads});
            std::cout << "Note: linear uses one independent arena per thread; no shared allocator contention.\n";
        }

        std::cout << "\n=== Optional memory maps (small illustrative instances) ===\n";
        {
            FreeListAllocator a(4096);
            void* p1 = a.Allocate(64, kDefaultAlignment);
            void* p2 = a.Allocate(128, kDefaultAlignment);
            a.Free(p1);
            (void)p2;
            a.PrintMemoryMap();
        }
        {
            PoolAllocator a(64, 64 * 8);
            void* p1 = a.Allocate(64);
            void* p2 = a.Allocate(64);
            a.Free(p1);
            (void)p2;
            a.PrintMemoryMap();
        }
        {
            LinearAllocator a(4096);
            a.Allocate(64, kDefaultAlignment);
            a.Allocate(128, kDefaultAlignment);
            a.PrintMemoryMap();
        }
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
