I had this project in mind but did not know what steps to follow so I asked Gemini to create a step by step plan for making custom memory allocator.

CUSTOM MEMORY ALLOCATOR IN C++, GIVING USERS CHOICE TO MAKE THEIR OWN MEMORY ALLOCATOR

CREDITS-”GEMINI”

Module 1: The Contract (The Interface)
Goal: Create a common definition that allows your program to treat all allocators exactly the same, even though they work differently under the hood.
Concept: Polymorphism & Abstract Base Classes. You need a parent class that defines the rules, but implements no logic itself.
Research Topics:
    • C++ Pure Virtual Functions (virtual void func() = 0;).
    • C++ Virtual Destructors (Why are they necessary in base classes?).
    • size_t and void* types.
Implementation Checklist:
    1. [ ] Create a class named Allocator.
    2. [ ] Add a protected variable m_start_ptr (to hold the start of the memory block) and m_total_size.
    3. [ ] Define a pure virtual method Allocate that takes a size and alignment, and returns a void*.
    4. [ ] Define a pure virtual method Free that takes a void*.
    5. [ ] Define a virtual method Init to pre-allocate the big chunk of memory (using malloc) that your allocator will manage.

Module 2: The Linear Allocator (The "Arena")
Goal: Implement the simplest allocator. It only moves a pointer forward.
Concept: "Bump Pointer" allocation. You keep an integer (offset) telling you how much memory is used. To allocate, you just return start_ptr + current_offset and increase the offset. You generally cannot free individual blocks here, only reset the whole thing.
Research Topics:
    • Pointer Arithmetic in C++ (Adding integers to pointers).
    • reinterpret_cast vs static_cast.
Implementation Checklist:
    1. [ ] Create LinearAllocator inheriting from Allocator.
    2. [ ] Add a member variable m_offset (starts at 0).
    3. [ ] Implement Allocate:
        ◦ Calculate the current address: start_ptr + m_offset.
        ◦ Check if m_offset + size > total_size (Out of memory check).
        ◦ Update m_offset += size.
        ◦ Return the calculated address.
    4. [ ] Implement Free: Leave this empty (or just verify the pointer exists), as Linear allocators don't free individual items.

Module 3: The Pool Allocator (Fixed Size)
Goal: An allocator optimized for many objects of the exact same size (e.g., Particle effects, projectiles).
Concept: The "Intrusive Linked List." Since the memory chunks are free, you don't need a separate list to track them. You store the "next free block" pointer inside the free memory block itself.
Research Topics:
    • Unions in C++ (specifically anonymous unions).
    • Intrusive Linked Lists.
    • "Free List" data structures.
Implementation Checklist:
    1. [ ] Create PoolAllocator inheriting from Allocator.
    2. [ ] Add a member variable m_chunkSize (passed in constructor).
    3. [ ] Implement Init:
        ◦ Loop through your memory.
        ◦ Divide it into chunkSize blocks.
        ◦ Write a pointer in the first block pointing to the second, the second to the third, etc.
    4. [ ] Implement Allocate:
        ◦ Take the first block from the list.
        ◦ Move the "head" pointer to the next block.
        ◦ Return the taken block.
    5. [ ] Implement Free:
        ◦ Take the pointer being freed.
        ◦ Write the current "head" address into this block.
        ◦ Make this block the new "head".

Module 4: The Free List Allocator (General Purpose)
Goal: The complex one. Handles any sreinterpret_cast<AllocationHeader*>
ize, effectively a mini-version of malloc.
Concept: You need to embed a "Header" before every allocation to track how big it is. You will need to manage a linked list of free blocks, search that list for a hole big enough, and split that hole.
Research Topics:
    • Memory Alignment (Bitwise operations to align to 8 or 16 bytes).
    • Struct Padding.
    • First-Fit vs Best-Fit Search Algorithms.
    • Coalescing (Merging adjacent memory blocks).
Implementation Checklist:
    1. [ ] Define a AllocationHeader struct (Size, Adjustment, pointer to next free block).
    2. [ ] Create FreeListAllocator inheriting from Allocator.
    3. [ ] Implement Allocate:
        ◦ Calculate total size needed (User Size + Header Size).
        ◦ Iterate through the linked list of free blocks.
        ◦ Find a block where block.size >= needed.
        ◦ Task: Implement "Splitting" (If the block is too big, cut off the extra and make a new free block).
        ◦ Return pointer to the body (not the header!).
    4. [ ] Implement Free:
        ◦ Math logic: HeaderPtr = UserPtr - sizeof(Header).
        ◦ Add this block back to the linked list.
        ◦ Task: Implement "Coalescing" (Check if the new free block is touching another free block and merge them).

Module 5: The Factory (The Generator)
Goal: A centralized place to create allocators based on user configuration.
Concept: The Factory Pattern. It isolates the complex logic of "which class do I create?" from the main logic of your program.
Research Topics:
    • Factory Method Design Pattern.
    • C++ enum class.
    • Static methods.
Implementation Checklist:
    1. [ ] Create an enum class AllocatorType { Linear, Pool, FreeList };.
    2. [ ] Create a struct AllocatorSettings (contains type, total size in bytes, chunk size).
    3. [ ] Create a class AllocatorFactory.
    4. [ ] Implement MakeAllocator:
        ◦ Static method accepting AllocatorSettings.
        ◦ Use a switch statement on the type.
        ◦ Return a Allocator* (pointer to the base class).

Module 6: The User Interface (The Main Loop)
Goal: A console application to test your creation.
Research Topics:
    • std::cin and input validation.
    • void* casting for std::cout (to print memory addresses).
Implementation Checklist:
    1. [ ] Ask user for Type, Size, and specific configs (chunk size).
    2. [ ] Call AllocatorFactory::MakeAllocator.
    3. [ ] Enter a while(true) loop.
    4. [ ] specific commands:
        ◦ "alloc [size]" -> Calls allocator->Allocate(size). Print the address returned.
        ◦ "free [address]" -> Calls allocator->Free(address).
    5. [ ] Bonus: Write a PrintMemoryMap() function in the base class that prints the status of the memory pool (e.g., [UUUU----UU--]) to visualize fragmentation.
Recommended Development Order
    1. Module 1 (Interface) + Module 2 (Linear): Get the basics working. You should be able to allocate memory simply.
    2. Module 6 (UI): Build the UI so you can test Module 2 interactively.
    3. Module 5 (Factory): Connect the UI to the Factory.
    4. Module 3 (Pool): Add the Pool logic.
    5. Module 4 (Free List): The final boss. Save this for last as it is the hardest.


My Recommendation
Start without it, then add it for Module 3.
    1. Phase 1 (Interface + Linear): Just use a simple g++ command. You only have 2-3 files. It's fast and keeps you focused on the C++ code.
    2. Phase 3 (Factory + Pool + UI): Once you have 5+ files, typing the compile command gets annoying. That is the perfect moment to "upgrade" to CMake. This mimics the actual evolution of software projects.


More implementations
Add benchmarking more rigid and see how they work well with chrono timsetting
Make their versions which are compatible with STL based DS
Make their version which work well with multithreaded applications using mutexes and etc.
For ex-two threads runnign at the same time might race to increase the offset for allocated memory creating an issue.
Define various error messages in code and order them in an enum.
Make a new module to add my own implementations of Googles tcmalloc
https://medium.com/@kondam.reddy/a-deep-dive-into-building-a-memory-allocator-dd5333a98195

try to adjust struct padding into mind

try to work on the internal fragmentation thing, maybe this method could be used→
Take note of all the internally fragmentated pointer vs size and then store it and maybe use it later..
