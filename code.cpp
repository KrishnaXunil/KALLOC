#include<iostream>
#include<cstddef>
#include<cstdint>
#include<sys/mman.h>

class Allocator{

protected:

void* m_start_ptr=nullptr;  //hold the start of the memory block
size_t m_total_size=0;   

public:

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


class LinearAllocator: public Allocator{

private:

size_t m_offset=0;

public:

LinearAllocator(size_t memory_size):Allocator(memory_size){}   //while calling constructor we have to call parent constructor tooooo...

void* Allocate(size_t size,size_t alignment){

    void* cur_address=(static_cast<char*>(m_start_ptr)); //doing static cast into char* moves the start ptr exactly in bytes amount

    size_t padding=(alignment-(reinterpret_cast<std::uintptr_t>(cur_address)%alignment))%alignment;

    if(m_offset+size+padding>m_total_size){
        //here throw some error
        return nullptr;
    }

    else{
        m_offset+=(size+padding);
        return (static_cast<char*>(cur_address)+padding);
    }

    return nullptr;
}

void Free(void* memory_ptr){

    //reinterpret cast does this
    //I know this variable is a Pointer (a memory address). I want you to 
    // simply take the bits that represent that address and treat them as an Integer.

    //here we cant compare pointers with each other so we need to cast them explicitly
    //as void* is an address with no type information and two things without any type cant be compared

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

int main() {
    // 1. Create an allocator with 100 bytes of total RAM
    std::cout << "Creating Allocator with 100 bytes..." << std::endl;
    LinearAllocator myAllocator(100); 

    // 2. Test a Valid Allocation (sizeof(int) is usually 4 bytes)
    std::cout << "Allocating 4 bytes for an Integer..." << std::endl;
    void* raw_memory = myAllocator.Allocate(sizeof(int), 0); // Alignment 0 for now

    if (raw_memory != nullptr) {
        // CAST the raw memory to an int* so we can use it
        int* myNumber = static_cast<int*>(raw_memory);
        
        // WRITE to the memory (if this crashes, your allocator is broken)
        *myNumber = 42; 
        
        // READ from the memory
        std::cout << "Success! Value stored: " << *myNumber << std::endl;
        std::cout << "Address: " << raw_memory << std::endl;
    } else {
        std::cerr << "Allocation failed!" << std::endl;
    }

    // 3. Test "Out of Memory" (Try to grab 200 bytes, which is > 100)
    std::cout << "\nAttempting to allocate 200 bytes (should fail)..." << std::endl;
    void* too_big = myAllocator.Allocate(200, 0);

    if (too_big == nullptr) {
        std::cout << "Success! The allocator correctly returned nullptr (Out of Memory)." << std::endl;
    } else {
        std::cerr << "Failed! The allocator gave us memory we don't have!" << std::endl;
    }

    //call destructor over this 

    myAllocator.Reset();


    LinearAllocator myAllocator1(100); // 100 bytes total

    std::cout << "1. Initial State:" << std::endl;
    myAllocator1.PrintMemoryMap();

    std::cout << "2. Allocating 40 bytes..." << std::endl;
    myAllocator1.Allocate(40, 0);
    myAllocator1.PrintMemoryMap();

    std::cout << "3. Allocating another 20 bytes..." << std::endl;
    myAllocator1.Allocate(20, 0);
    myAllocator1.PrintMemoryMap();

    return 0;
}