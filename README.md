# Custom Memory Allocator in C++

A customizable memory allocation library in C++ that allows users to choose different memory allocation strategies based on their application's requirements.

The goal of this project is to understand how memory allocators work internally and implement multiple allocation strategies behind a common interface, allowing users to switch between them without changing application-level code.

## Credits

The initial step-by-step development plan was generated with the help of **Google Gemini**. The implementation, experimentation, and further extensions are part of this project.

## Project Overview

Standard dynamic memory allocation in C++ is typically handled through `new`/`delete` or the underlying memory management facilities. However, different applications have different allocation patterns, and a single allocation strategy may not be optimal for every workload.

This project explores how custom allocators can improve control over memory usage, allocation overhead, fragmentation, and allocation patterns.

The library is designed around a common `Allocator` interface and multiple concrete implementations. An `AllocatorFactory` allows users to select an allocator through configuration rather than depending directly on its implementation.

### Key Objectives

- Implement multiple memory allocation strategies behind a common interface.
- Understand memory alignment, pointer arithmetic, free lists, and fragmentation.
- Provide a configurable interface for selecting allocation strategies.
- Build an interactive console application to test allocations and deallocations.
- Benchmark different allocators under different workloads.
- Explore thread safety and compatibility with STL containers.
- Investigate advanced allocation strategies inspired by production allocators such as Google's TCMalloc.

## Architecture

The project is divided into six modules, each responsible for a specific part of the allocator system.

| Module | Component | Responsibility |
|---|---|---|
| 1 | `Allocator` | Defines the common allocator interface |
| 2 | `LinearAllocator` | Provides fast bump-pointer allocation |
| 3 | `PoolAllocator` | Efficiently allocates fixed-size blocks |
| 4 | `FreeListAllocator` | Supports variable-sized allocations and block reuse |
| 5 | `AllocatorFactory` | Creates allocators from user configuration |
| 6 | Console Interface | Allows users to configure and test allocators interactively |

## Module 1: The Contract — Allocator Interface

**Goal:** Define a common interface that allows the application to use different allocators polymorphically.

The base `Allocator` class manages the underlying memory region and defines the operations that every allocator must support.

### Responsibilities

- Maintain the starting address of the memory region using `m_start_ptr`.
- Track the total available memory using `m_total_size`.
- Define a pure virtual `Allocate` method accepting a size and alignment.
- Define a pure virtual `Free` method accepting a memory pointer.
- Provide initialization and cleanup for the managed memory region.
- Use a virtual destructor to ensure correct destruction through base-class pointers.

### Concepts to Explore

- Abstract classes and pure virtual functions.
- Virtual destructors and polymorphism.
- `size_t` and `void*`.
- Memory ownership and resource management.

## Module 2: Linear Allocator — The Arena

**Goal:** Implement a simple and fast allocator using a bump pointer.

A linear allocator maintains an offset into a preallocated memory region. Each allocation advances the offset, making allocation inexpensive and predictable.

### Allocation Strategy

1. Calculate the next available address from the base pointer and current offset.
2. Account for the requested alignment.
3. Check whether sufficient memory remains.
4. Return the aligned address and advance the offset.
5. Reset the allocator to reuse the entire region when individual deallocation is unnecessary.

Individual allocations generally cannot be reclaimed. The entire arena can be reset when its contents are no longer needed.

### Implementation Checklist

- [ ] Implement `LinearAllocator` inheriting from `Allocator`.
- [ ] Maintain an `m_offset` member initialized to zero.
- [ ] Implement bounds checking and alignment-aware allocation.
- [ ] Implement a reset operation.
- [ ] Define the behavior of `Free` explicitly.

### Concepts to Explore

- Pointer arithmetic.
- `reinterpret_cast` and `static_cast`.
- Alignment and padding.
- Arena allocation and lifetime management.

## Module 3: Pool Allocator — Fixed-Size Allocation

**Goal:** Efficiently allocate and recycle objects of a fixed size, such as particles, projectiles, or frequently created game objects.

The pool allocator divides a preallocated memory region into equal-sized blocks and tracks available blocks using an intrusive free list.

### Allocation Strategy

1. Divide the memory region into appropriately aligned blocks.
2. Link the available blocks together.
3. Maintain a pointer to the first available block.
4. Allocate by removing the first block from the free list.
5. Deallocate by returning the block to the list.

The free-list link can be stored directly inside an unused block, avoiding a separate allocation for free-list metadata.

### Implementation Checklist

- [ ] Implement `PoolAllocator` inheriting from `Allocator`.
- [ ] Configure the chunk size and alignment.
- [ ] Initialize the free list during allocator initialization.
- [ ] Implement constant-time allocation and deallocation.
- [ ] Validate that freed pointers belong to the pool and are correctly aligned.
- [ ] Handle exhaustion of available blocks.

### Concepts to Explore

- Intrusive linked lists.
- Free-list data structures.
- Unions and object lifetime rules.
- Alignment requirements for metadata and allocated objects.

## Module 4: Free List Allocator — General-Purpose Allocation

**Goal:** Support variable-sized allocations and reuse freed memory blocks.

This allocator maintains a linked list of available memory blocks. It searches for a suitable block, allocates the requested space, and splits unused portions when possible.

### Core Components

- **Free block metadata:** Tracks the size of each available block and the next free block.
- **Allocation header:** Stores information required to recover the allocation's metadata during deallocation.
- **Placement and alignment logic:** Ensures returned addresses meet the requested alignment.
- **Block splitting:** Divides a sufficiently large free block into an allocated portion and a remaining free portion.
- **Coalescing:** Merges adjacent free blocks to reduce external fragmentation.

### Allocation Strategy

1. Calculate the total space required, including alignment adjustments and metadata.
2. Search the free list using a strategy such as first-fit or best-fit.
3. Select a suitable block.
4. Split the block if the remaining space can form a valid free block.
5. Store the allocation metadata and return the aligned user pointer.

### Deallocation Strategy

1. Recover the allocation metadata associated with the supplied pointer.
2. Validate the allocation where practical.
3. Return the block to the free list.
4. Merge adjacent free blocks when possible.

### Implementation Checklist

- [ ] Define the allocation and free-block metadata structures.
- [ ] Implement `FreeListAllocator` inheriting from `Allocator`.
- [ ] Implement aligned allocation and deallocation.
- [ ] Implement first-fit or best-fit searching.
- [ ] Implement block splitting.
- [ ] Implement coalescing of adjacent free blocks.
- [ ] Handle allocation failures and invalid frees.
- [ ] Track fragmentation and metadata overhead.

### Concepts to Explore

- Memory alignment and bitwise operations.
- Structure padding and layout.
- First-fit versus best-fit algorithms.
- Internal versus external fragmentation.
- Free-list organization and block coalescing.

## Module 5: The Factory — Allocator Creation

**Goal:** Separate allocator selection and construction from the rest of the application.

The factory creates an allocator based on a user-provided configuration. This allows the client code to work with the base `Allocator` interface without knowing the concrete implementation.

### Proposed Components

- `AllocatorType`: An enumeration containing `Linear`, `Pool`, and `FreeList`.
- `AllocatorSettings`: A configuration structure containing the allocator type, total memory size, chunk size, and other strategy-specific settings.
- `AllocatorFactory`: A class responsible for constructing the requested allocator.

### Implementation Checklist

- [ ] Define the `AllocatorType` enumeration.
- [ ] Define the `AllocatorSettings` structure.
- [ ] Implement `AllocatorFactory::MakeAllocator`.
- [ ] Validate allocator-specific settings.
- [ ] Handle unsupported types and initialization failures.
- [ ] Define clear ownership and cleanup rules for created allocators.

## Module 6: The User Interface — Interactive Testing

**Goal:** Build a console application that lets users configure an allocator and experiment with memory operations.

### Proposed Workflow

1. Ask the user to select an allocator type.
2. Request the total memory size and any strategy-specific parameters.
3. Create the allocator through `AllocatorFactory`.
4. Accept allocation and deallocation commands.
5. Display returned addresses, errors, and memory usage.
6. Optionally visualize the current memory layout.

### Example Commands

```text
alloc 64
alloc 128
free <address>
reset
stats
map
exit
```

The exact command set can evolve as the allocator implementations mature.

### Additional Features

- `stats`: Display total memory, used memory, free memory, and allocation counts where supported.
- `map`: Visualize allocated and free regions, for example:

```text
[UUUU----UU--]
```

Here, `U` represents allocated memory and `-` represents free memory. The visualization should reflect the actual allocation layout rather than assuming all allocators have identical bookkeeping.

- Error reporting for out-of-memory conditions, invalid pointers, and unsupported operations.

## Recommended Development Order

Implement the project incrementally so that each stage builds on a working foundation.

1. **Interface + Linear Allocator:** Establish the base class and get basic allocation working.
2. **Console Interface:** Build an interactive way to test allocations and reset operations.
3. **Factory:** Connect allocator selection to the console interface.
4. **Pool Allocator:** Add efficient fixed-size allocation and recycling.
5. **Free List Allocator:** Implement variable-size allocation, splitting, and coalescing.
6. **Testing and Benchmarking:** Compare allocator behavior under different workloads.
7. **Advanced Extensions:** Explore concurrency, STL compatibility, and fragmentation analysis.

## Build System

### Initial Development

Start with a simple compiler command while the project contains only a few source files.

```bash
g++ -std=c++17 -Wall -Wextra -g src/*.cpp -Iinclude -o allocator_demo
./allocator_demo
```

Adjust the source paths as the repository structure evolves.

### CMake Integration

Introduce CMake once the project contains multiple allocator implementations, headers, tests, and supporting utilities. This makes the project easier to build, maintain, and extend.

A future build configuration can include separate targets for the allocator library, demo application, unit tests, and benchmarks.

## Testing and Benchmarking

A major goal is to compare allocator behavior across realistic workloads rather than assuming that a custom allocator is always faster than standard allocation.

### Benchmarking Plan

Use C++'s `<chrono>` library to measure allocation and deallocation performance.

Benchmark scenarios should include:

- Repeated allocations of small, fixed-size objects.
- Mixed allocation sizes.
- Different allocation and deallocation patterns.
- High allocation counts and memory exhaustion.
- Fragmented memory layouts.
- Reset-heavy arena workloads.
- Concurrent allocation workloads after thread safety is implemented.

### Metrics to Measure

- Allocation and deallocation latency.
- Total execution time.
- Throughput in operations per second.
- Memory utilization.
- Internal and external fragmentation.
- Metadata overhead.
- Allocation failures under constrained memory budgets.

For meaningful comparisons, use the same workloads across implementations, repeat experiments, and report the methodology and results.

## Future Enhancements

### 1. STL-Compatible Allocators

Implement allocator adapters that allow compatible custom allocators to be used with standard containers such as `std::vector`, `std::list`, and `std::unordered_map`.

Investigate the standard allocator requirements and how container allocation patterns affect performance.

### 2. Thread-Safe Allocators

Add support for concurrent allocation and deallocation.

For example, two threads using a linear allocator simultaneously could read the same offset and attempt to allocate overlapping memory regions unless the operation is synchronized.

Potential approaches include:

- Mutex-based synchronization.
- Atomic bump-pointer allocation where alignment and bounds checks are handled safely.
- Per-thread arenas.
- Thread-local pools and reduced contention.

Benchmark the performance cost of synchronization and compare it with approaches that minimize shared state.

### 3. Error Handling

Define a centralized error-code enumeration for common failure conditions, such as:

- Out of memory.
- Invalid alignment.
- Invalid or foreign pointer.
- Double free.
- Unsupported allocator operation.
- Invalid configuration.
- Initialization failure.

Design a consistent error-reporting mechanism across allocator implementations.

### 4. Internal Fragmentation Analysis

Track the difference between requested allocation sizes and the actual memory consumed by each allocation.

For example, an allocation requesting 13 bytes might require more space because of alignment or metadata.

Potential improvements:

- Record requested size, usable size, and total consumed size.
- Track alignment and metadata overhead.
- Analyze recurring allocation patterns.
- Investigate whether unused space can be reused safely.
- Compare utilization across allocation strategies.

Any reuse mechanism must preserve allocation boundaries, object alignment, and memory safety.

### 5. Advanced Allocator Implementations

Create additional modules inspired by production memory allocators, including Google's TCMalloc.

Investigate concepts such as size classes, thread-local caches, central free lists, and page-level allocation. Implement these as independent strategies that conform to the project's common interface.

**Reference:** [A Deep Dive into Building a Memory Allocator](https://medium.com/@kondam.reddy/a-deep-dive-into-building-a-memory-allocator-dd5333a98195)

### 6. Memory Layout and Structure Padding

Study how structure layout, alignment, and padding influence allocator metadata overhead and usable memory.

Experiment with different metadata layouts and measure their impact rather than assuming that smaller structures always lead to better overall performance.

### 7. Memory Visualization and Diagnostics

Extend the memory-map functionality to show:

- Allocated and free regions.
- Block sizes and alignment adjustments.
- Free-list structure.
- Fragmentation over time.
- Allocation and deallocation history.

## Project Goals

The long-term goal is to build an extensible memory allocation framework that combines multiple allocation strategies, a configurable interface, correctness testing, performance benchmarking, and detailed memory diagnostics.

The project is also intended as a learning exercise in low-level C++ programming, memory management, data structures, object-oriented design, and performance engineering.
