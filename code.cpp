#include<iostream>
#include<cstddef>
#include<cstdint>
#include<sys/mman.h>
#include <vector>
#include <iomanip>

class Allocator{

protected:

//protected because we need other allocators to access this but not outside of allocators
void* m_start_ptr=nullptr;  //hold the start of the memory block
size_t m_total_size=0;   

public:

//member initialisation lists
Allocator(size_t memory_size):m_total_size(memory_size){
     Init();
}

virtual void* Allocate(size_t size,size_t alignment)=0;

virtual void Free(void* memory_ptr)=0;

void Init(){
    m_start_ptr=mmap(nullptr,m_total_size,PROT_READ | PROT_WRITE, // Read/Write permissions
        MAP_PRIVATE | MAP_ANONYMOUS, // Anonymous = RAM, not file
        -1,                     // No file descriptor
        0);

    if (m_start_ptr == MAP_FAILED) {
        std::cerr << "CRITICAL ERROR: mmap failed! Out of memory?" << std::endl;
        m_start_ptr = nullptr; // Set to null so we can check it safely later
        exit(1); // Crash immediately or handle gracefully
    }
} //here this is not pure virtual because parent
//class can provide its own implementation

//other pure virtual function need to be defined by all allocators
//and Class Allocator cannot be thus instantiated by its own

virtual ~Allocator(){
//as verything is done using mmap we need to deallocate memory also
     if(m_start_ptr!=nullptr){
        munmap(m_start_ptr,m_total_size);
        m_start_ptr=nullptr;
     }
}

virtual void PrintMemoryMap() const=0;

};

//a mini version of malloc it seems
//this maybe implementation heavy and also may require testing over different search algorithms

//implement coalescing later, it maybe a bit difficult for now
class FreeListAllocator: protected Allocator{
     
private:

//again intrusive linked list only
struct AllocationHeader{
    size_t size;
    //adjustment stores the alignment issue
    //this will help us in FREE fucntion when we will 
    //have to recalculate the start of the memory area using 
    //the provided pointer-padding-header_size
    size_t adjustment;
    AllocationHeader* next;
};

AllocationHeader* head;

public:

FreeListAllocator(size_t memory_size): Allocator(memory_size){
    head=reinterpret_cast<AllocationHeader*>(m_start_ptr);
    head->size=memory_size;
    head->adjustment=0;
    head->next=nullptr;
}

void* Allocate(size_t size, size_t alignment) override {
    size_t required = size + sizeof(AllocationHeader);
    AllocationHeader* prev = nullptr;
    AllocationHeader* cur = head;

    // ... (Your existing search loop) ...

    size_t padding = (alignment - reinterpret_cast<size_t>(cur) % alignment) % alignment;
    size_t left = cur->size - padding;

    if (left < required) return nullptr;

    // Save the total size before we start shifting things
    size_t total_block_size = cur->size; 

    // Calculate where the header and data will live
    char* block_start = reinterpret_cast<char*>(cur);
    char* data_ptr = block_start + padding + sizeof(AllocationHeader);
    AllocationHeader* header_ptr = reinterpret_cast<AllocationHeader*>(data_ptr - sizeof(AllocationHeader));

    // Determine splitting
    size_t allocated_size = required + padding;

    if (left > required + sizeof(AllocationHeader)) {
        // Splitting logic
        AllocationHeader* next_free = reinterpret_cast<AllocationHeader*>(block_start + allocated_size);
        next_free->size = total_block_size - allocated_size;
        next_free->adjustment = 0;
        next_free->next = cur->next;
        
        // Update the list (simplified for this example)
        head = next_free; 
    } else {
        allocated_size = total_block_size;
        head = cur->next;
    }

    // --- THE FIX: STORE METADATA AT BOTH ENDS ---
    // 1. Tag the physical start with the padding size for the Map Walk
    *(reinterpret_cast<size_t*>(block_start)) = padding;

    // 2. Setup the header for the Free function
    header_ptr->size = allocated_size;
    header_ptr->adjustment = padding;
    header_ptr->next = nullptr; // Not in free list anymore

    return data_ptr; // same as pointer
}

void Free(void* memory_ptr) override {
    // 1. Find the header (it's always right behind the user pointer)
    AllocationHeader* header = reinterpret_cast<AllocationHeader*>(
        reinterpret_cast<uintptr_t>(memory_ptr) - sizeof(AllocationHeader)
    );

    // 2. Find the true start of the block using adjustment
    uintptr_t block_start = reinterpret_cast<uintptr_t>(header) - header->adjustment;
    
    // 3. Prepare the block to be a Free List Node
    AllocationHeader* freed_node = reinterpret_cast<AllocationHeader*>(block_start);
    
    // IMPORTANT: We must preserve the 'size' so the Map can still jump!
    // But we reset adjustment to 0 because a FREE block has no alignment offset.
    size_t original_total_size = header->size; 
    
    freed_node->size = original_total_size;
    freed_node->adjustment = 0; 
    freed_node->next = head;
    
    // 4. Update the Head
    head = freed_node;
}

void PrintMemoryMap() const override {
    std::cout << "\n================ PHYSICAL MEMORY MAP ================\n";
    std::cout << "Total Size: " << m_total_size << " bytes | Start: " << m_start_ptr << "\n";
    std::cout << "-----------------------------------------------------\n";

    uintptr_t current_address = reinterpret_cast<uintptr_t>(m_start_ptr);
    uintptr_t end_address = current_address + m_total_size;
    int block_count = 0;

    while (current_address < end_address) {
        // 1. Membership Check: Is this address in the Free List?
        bool is_free = false;
        AllocationHeader* free_curr = head;
        while (free_curr != nullptr) {
            if (reinterpret_cast<uintptr_t>(free_curr) == current_address) {
                is_free = true;
                break;
            }
            free_curr = free_curr->next;
        }

        size_t block_size = 0;
        size_t adjustment = 0;

        if (is_free) {
            // If free, the header is right here at the start
            AllocationHeader* h = reinterpret_cast<AllocationHeader*>(current_address);
            block_size = h->size;
            adjustment = 0;
        } else {
            // If allocated, we use your "padding tag" logic
            // We read the first few bytes. In your logic, this is either 'padding' or 'header.size'
            size_t first_val = *(reinterpret_cast<size_t*>(current_address));
            AllocationHeader* h = reinterpret_cast<AllocationHeader*>(reinterpret_cast<size_t>(current_address)+first_val);

            // If the header sits at the start, h->size will match our walk logic
            // We assume h->adjustment == 0 means the header is at the start
            // if (h->adjustment == 0) {
            //     block_size = h->size;
            //     adjustment = 0;
            // } else {
                // If adjustment != 0, then first_val is actually the padding (adjustment)
                std::cout<<"FUCK1"<<std::endl;
                adjustment = first_val; 
                std::cout<<"FUCK2"<<std::endl;
                std::cout<<first_val<<std::endl;
                std::cout<<"FUCK3"<<std::endl;
                block_size = h->size;
                std::cout<<"FUCK4"<<std::endl;
                std::cout<<block_size<<std::endl;
            // }
        }

        // std::cout<<"FUCK2"<<std::endl;

        // --- THE ACTUAL PRINTING STATEMENTS ---
        std::cout << "Block [" << std::setw(2) << block_count++ << "] "
                  << "Addr: " << (void*)current_address << " | "
                  << "Size: " << std::setw(6) << block_size << " | "
                  << "Status: " << (is_free ? "[ FREE ]" : "[ALLOCD]") 
                  << " | Padding: " << adjustment << "\n";

        // Safety break for corrupted memory
        if (block_size == 0) {
            std::cout << "[!] CRITICAL: Block size 0 at " << (void*)current_address << ". Breaking walk.\n";
            break;
        }

        current_address += block_size;
    }
    std::cout << "=====================================================\n\n";
}

};

class PoolAllocator: protected Allocator{
    
private:

size_t m_chunksize;

//revise on what intrusive linked list
struct chunk{
    chunk* next;
};

chunk* chunk_head;

public:

PoolAllocator(size_t chunk_size,size_t memory_size): Allocator(memory_size),m_chunksize(chunk_size){
    //now using member initialisation list again and now when parents constructor is called 
    //i.e Allocator, it will automatically call the init for it and define the starting pointer
    //for memory allocation i.e [void* m_start_ptr]

    //I will simply divide this memory chunks to smaller chunks leveraging 
    //the benefit of this constructor

    chunk_head=reinterpret_cast<chunk*>(m_start_ptr);

    chunk* prev=chunk_head;

    size_t num_of_chunks=(memory_size+chunk_size-1)/(chunk_size);

    for(int i=1;i<num_of_chunks;i++){
        void* location=reinterpret_cast<char*>(prev)+chunk_size;
        prev->next=reinterpret_cast<chunk*>(location);
        prev=prev->next;
    }
    prev->next=nullptr;
}

//here we dont care about alignment as we did in linear allocator, because the use of this pool
//allocator ensures we know the required alignment before only and thus we can adjust our chunk_size 
//by that alignment, but this alignment cant change at runtime

//in linear allocator alignment gets taken care of at runtime too.....
void* Allocate(size_t size,size_t alignment=8) override {

    if(chunk_head==nullptr){
        std::cerr << "CRITICAL ERROR: No memory left!" << std::endl;
        return nullptr;
    }

    size_t padding=(alignment-reinterpret_cast<size_t>(chunk_head)%alignment)%alignment;
    
    //this is the original head which was there with our linked list already
    //not the new one with the padding....
    chunk* actual_head=chunk_head;

    chunk_head=reinterpret_cast<chunk*>(reinterpret_cast<char*>(chunk_head)+padding);

    if(m_chunksize-padding<size){
        std::cerr << "This chunk cant be allocated for now try later!" << std::endl;
        return nullptr;
    }

    else{
       void* memory_to_be_given=chunk_head;
       chunk_head=actual_head->next;
       return memory_to_be_given;
    }
    
}

void Free(void* memory_ptr) override {
    // void* location=reinterpret_cast<char*>(mempry_ptr);

    char* base_location=static_cast<char*>(m_start_ptr);
    char* end_location=base_location+m_total_size;

    char* cur_location=static_cast<char*>(memory_ptr);

    size_t offset=cur_location-base_location;

    if(cur_location>=base_location && cur_location<end_location && offset%m_chunksize==0){
        chunk* new_head=reinterpret_cast<chunk*>(memory_ptr);
        new_head->next=chunk_head;
        chunk_head=new_head;
    }

    else if(offset%m_chunksize!=0){
        std::cerr << "CRITICAL ERROR: Pointer is misaligned in the memory pool!" << std::endl;
       return; // Reject it
    }

    else{
       std::cerr << "CRITICAL ERROR: Pointer is outside of memory pool!" << std::endl;
       return; // Reject it
    }

} 

void PrintMemoryMap() const override {
    // 1. Calculate how many total chunks we have in the pool
    // (Assuming m_total_size is available from the parent Allocator class)
    size_t num_chunks = m_total_size / m_chunksize;

    // 2. Create a temporary map to track status (Default: false = USED)
    std::vector<bool> is_chunk_free(num_chunks, false);

    // 3. Walk the Free List to find which blocks are actually available
    chunk* current = chunk_head;
    size_t free_count = 0;

    while (current != nullptr) {
        // MATH: Calculate the index of this chunk based on its address
        // Index = (Address of Chunk - Start of Pool) / Size of Chunk
        char* start_ptr = static_cast<char*>(m_start_ptr);
        char* chunk_ptr = reinterpret_cast<char*>(current);
        
        size_t byte_offset = chunk_ptr - start_ptr;
        size_t chunk_index = byte_offset / m_chunksize;

        // Safety check: Ensure index is valid
        if (chunk_index < num_chunks) {
            is_chunk_free[chunk_index] = true; // Mark this specific block as FREE
            free_count++;
        }
        
        // Move to next free block
        current = current->next;
    }

    // 4. Print the Visualization
    std::cout << "\n[ Pool Memory Map ]" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;
    
    for (size_t i = 0; i < num_chunks; ++i) {
        // Optional: Print a newline every 10 blocks for readability
        if (i > 0 && i % 10 == 0) std::cout << "\n";

        if (is_chunk_free[i]) {
            std::cout << "[ . ] "; // . means Empty/Free
        } else {
            std::cout << "[ # ] "; // # means Occupied/Used
        }
    }
    std::cout << "\n------------------------------------------------" << std::endl;
    
    // 5. Print Stats
    size_t used_count = num_chunks - free_count;
    double usage = (static_cast<double>(used_count) / num_chunks) * 100.0;
    
    std::cout << "Total Blocks: " << num_chunks 
              << " | Used: " << used_count 
              << " | Free: " << free_count 
              << " (" << std::fixed << std::setprecision(1) << usage << "% Full)" 
              << std::endl;
    std::cout << std::endl;
}

};


class LinearAllocator: protected Allocator{

private:

size_t m_offset=0;

public:

//again member initialisation lists
LinearAllocator(size_t memory_size):Allocator(memory_size){}   //while calling constructor we have to call parent constructor tooooo...

void* Allocate(size_t size,size_t alignment) override {
    
    //again dividing by alignment so that padding becomes zero if already alignment satisfies 
    size_t padding=(alignment-(reinterpret_cast<std::uintptr_t>(m_start_ptr)%alignment))%alignment;

    void* cur_address=((static_cast<char*>(m_start_ptr))+padding); //doing static cast into char* moves the start ptr exactly in bytes amount

    if(m_offset+size+padding>m_total_size){
        //here throw some error
        return nullptr;
    }

    else{
        m_offset+=(size+padding);
        return cur_address;
    }

    return nullptr;
}

void Free(void* memory_ptr) override {

    //reinterpret cast does this
    //I know this variable is a Pointer (a memory address). I want you to 
    // simply take the bits that represent that address and treat them as an Integer.

    //here we cant compare pointers with each other so we need to cast them explicitly
    //as void* is an address with no type information and two things without any type cant be compared

    //an idea i had for Free function was to store all instances/range of linearly allocated memeory
    //and then simply shifting everything to its right, left 
    //but this would cost in complexity

    std::uintptr_t start = reinterpret_cast<std::uintptr_t>(m_start_ptr);
    std::uintptr_t end = start + m_total_size;
    std::uintptr_t current = reinterpret_cast<std::uintptr_t>(memory_ptr);
       
    if(current>=start && current<=end){
        //yes valid
    }

    else{
        //free doesnt make sense
    }

}

// Inside LinearAllocator class
void PrintMemoryMap() const override {
    std::cout << "\n[ Memory Map (Linear) ]" << std::endl;
    std::cout << "Start: " << m_start_ptr << ", Size: " << m_total_size << ", Offset: " << m_offset << std::endl;

    // We want a bar that is 50 characters long
    const size_t width = 50;
    double bytes_per_char = (double)m_total_size / width;

    std::cout << "[";
    for (size_t i = 0; i < width; ++i) {
        // Calculate the memory boundary for this character
        size_t current_byte_index = (size_t)(i * bytes_per_char);

        // Linear Logic: If we are below the offset, it is USED.
        if (current_byte_index < m_offset) {
            std::cout << "#"; // Used
        } else {
            std::cout << "."; // Free
        }
    }
    std::cout << "]" << std::endl;
    
    // Calculate percentage
    double usage = ((double)m_offset / m_total_size) * 100;
    std::cout << "Usage: " << usage << "%\n" << std::endl;
}

void Reset() {
    m_offset = 0;
    std::cout << "Linear Allocator Reset!" << std::endl;
}


};

// template<typename t>
// void printname(T name){
//     std::cout<<name<<std::endl;
// }

// int main() {

//     LinearAllocator myAllocator1(100); // 100 bytes total

//     std::cout << "1. Initial State:" << std::endl;
//     myAllocator1.PrintMemoryMap();

//     std::cout << "2. Allocating 40 bytes..." << std::endl;
//     myAllocator1.Allocate(40, 3);
//     myAllocator1.PrintMemoryMap();

//     std::cout << "3. Allocating another 20 bytes..." << std::endl;
//     myAllocator1.Allocate(20, 11);
//     myAllocator1.PrintMemoryMap();

//     return 0;
// }

// int main() {
//     // 1. Setup: Create a pool for 10 integers (chunk size 8, total size 80)
//     // We use size 80 bytes, so we get 10 chunks of 8 bytes.
//     const size_t CHUNK_SIZE = 8;
//     const size_t TOTAL_SIZE = 80;
    
//     PoolAllocator allocator1(CHUNK_SIZE, TOTAL_SIZE);

//     std::cout << "1. Initial State (Everything Free):" << std::endl;
//     allocator1.PrintMemoryMap();

//     // 2. Allocate 3 blocks
//     void* p1 = allocator1.Allocate(CHUNK_SIZE);
//     void* p2 = allocator1.Allocate(CHUNK_SIZE);
//     void* p3 = allocator1.Allocate(CHUNK_SIZE);

//     std::cout << "2. After Allocating 3 Blocks (Should see 3 used [#]):" << std::endl;
//     allocator1.PrintMemoryMap();

//     // 3. Free the Middle Block (p2)
//     // This demonstrates that the Free List works (LIFO) and the map updates correctly
//     allocator1.Free(p2);

//     std::cout << "3. After Freeing Middle Block (p2) (Should see a hole [.]):" << std::endl;
//     allocator1.PrintMemoryMap();

//     // 4. Re-allocate
//     // It should grab the hole we just created because it was added to the front of the list!
//     void* p4 = allocator1.Allocate(CHUNK_SIZE);
    
//     std::cout << "4. After Re-allocating (Should fill the hole):" << std::endl;
//     allocator1.PrintMemoryMap();

//     return 0;
// }

int main() {
    // 1. Setup: Grab a larger chunk of memory (e.g., 1024 bytes)
    // Unlike a Pool Allocator, we don't define a chunk size, because chunks can be any size!
    const size_t TOTAL_SIZE = 1024;
    const size_t ALIGNMENT = 8;
    
    std::cout << "Starting Free List Allocator Test...\n";
    FreeListAllocator allocator(TOTAL_SIZE);



    std::cout << "1. Initial State (Should see ONE giant free block):" << std::endl;
    allocator.PrintMemoryMap();

    //  return 0;

    // 2. Allocate 3 blocks of DIFFERENT sizes
    std::cout << "\nAllocating Block A (64 bytes), B (128 bytes), and C (256 bytes)..." << std::endl;
    void* pA = allocator.Allocate(64, 7);
    void* pB = allocator.Allocate(128, ALIGNMENT);
    void* pC = allocator.Allocate(256, ALIGNMENT);

    // return 0;

    std::cout << "2. After Allocating 3 Blocks:" << std::endl;
    // You should only see ONE free block left: the remaining space at the very end of your 1024 bytes.
    allocator.PrintMemoryMap();

    // return 0;

    // 3. Free the Middle Block (pB - 128 bytes)
    std::cout << "\nFreeing Middle Block B (128 bytes)..." << std::endl;
    // This demonstrates that the Free List can handle random free orders and tracks the holes.
    allocator.Free(pB);

    std::cout << "3. After Freeing Middle Block B:" << std::endl;
    // You should now see TWO free blocks: The hole where B used to be, and the remaining space at the end.
    allocator.PrintMemoryMap();

    // return 0;

    // 4. Re-allocate a smaller block (32 bytes)
    std::cout << "\nAllocating Block D (32 bytes)..." << std::endl;
    // It should iterate, find the 128-byte hole we just created, put Block D there, and SPLIT the difference!
    void* pD = allocator.Allocate(32, ALIGNMENT);
    
    std::cout << "4. After Re-allocating Block D:" << std::endl;
    // The first free block (the hole) should now be smaller, because D took up part of it.
    allocator.PrintMemoryMap();

    // Clean up to prevent actual OS memory leaks during testing
    allocator.Free(pA);
    allocator.PrintMemoryMap();
    allocator.Free(pC);
    allocator.PrintMemoryMap();
    allocator.Free(pD);
    allocator.PrintMemoryMap();

    return 0;
}