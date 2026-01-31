#include<iostream>
#include<cstddef>
#include<cstdint>
#include<sys/mman.h>

class Allocator{

protected:

//protected because we need other allocators to access this but not outside of allocators
void* m_start_ptr=nullptr;  //hold the start of the memory block
size_t m_total_size=0;   
const int x;

public:

//member initialisation lists
Allocator(size_t memory_size,int val):m_total_size(memory_size), x(5){
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

class PoolAllocator: public Allocator{

private:

size_t m_chunksize;
void* head;

public:

struct Chunk{
    Chunk* next;
};

PoolAllocator(size_t memory_size):Allocator(memory_size){
    size_t num_of_chunks=(memory_size+m_chunksize-1)/m_chunksize;

    head=m_start_ptr;

    for(int i=1;i<=num_of_chunks;i++){
       Chunk 
    }
}

}

class LinearAllocator: public Allocator{

private:

size_t m_offset=0;

public:

//again member initialisation lists
LinearAllocator(size_t memory_size):Allocator(memory_size){}   //while calling constructor we have to call parent constructor tooooo...

void* Allocate(size_t size,size_t alignment){

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

void Free(void* memory_ptr){

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

template<typename t>
void printname(T name){
    std::cout<<name<<std::endl;
}

int main() {

    LinearAllocator myAllocator1(100); // 100 bytes total

    std::cout << "1. Initial State:" << std::endl;
    myAllocator1.PrintMemoryMap();

    std::cout << "2. Allocating 40 bytes..." << std::endl;
    myAllocator1.Allocate(40, 3);
    myAllocator1.PrintMemoryMap();

    std::cout << "3. Allocating another 20 bytes..." << std::endl;
    myAllocator1.Allocate(20, 11);
    myAllocator1.PrintMemoryMap();

    return 0;
}