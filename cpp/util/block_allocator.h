// Copyright 2014-2014 the openage authors. See copying.md for legal info.
#ifndef OPENAGE_UTIL_BLOCK_ALLOCATOR_H_
#define OPENAGE_UTIL_BLOCK_ALLOCATOR_H_
#include <type_traits>
#include <cstddef>
#include <utility>
#include <memory>
#include <vector>
#include <new>

#include "compiler.h"
namespace openage{
namespace util{
/**
 * This is a allocator for single allocations that returns memory out of pre-allocated
 * blocks instead of allocating memory on each call to new. This makes
 * it fairly cheap to allocate many small objects, and also greatly
 * improves cache coherency
 *
 * Object allocated by this do not live longer than the duration of
 * the allocator. However, destructors must be called manually as with delete
 * if they perform meaningful work
 *
 * When block_limit <= 0, the allocator will continuosly allocate more
 * blocks when needed. If block_limit > 0, then that will be the limit
 * set on the number of blocks. The maximum number of blocks is
 * max(index_type), which defaults to 2^32. index_type must be unsigned
 *
 * The number of bytes taken up by each allocation is
 * max(sizeof(void*), sizeof(T) + sizeof(index_type))
 */
template<class T, class index_type = uint32_t>
class block_allocator{
protected:
    static_assert(std::is_unsigned<index_type>::value, "index type must be unsigned");
    //!Holds the union of data and next free node
    typedef union val_ptr {
	struct{
	    char values[sizeof(T)];//avoids any constructors being called
	    index_type block_index;
	} data;
	val_ptr* next;
    } val_ptr;

    //!struct that contains the memory being allocated
    struct _block{
	std::unique_ptr<val_ptr[]> data;
	val_ptr* first_open;
	_block(size_t data_len);
    };
    //!Attempts to retrieve a pointer, returns 0 otherwise.
    T* _get_ptr();

    //!size of each block
    size_t block_size;

    //!list of memory blocks
    std::vector<_block> blocks;

    //!limit on the amount of blocks
    index_type block_limit;
public:
    block_allocator(const block_allocator&) = delete;
    block_allocator operator=(const block_allocator&) = delete;
    
    block_allocator(block_allocator&&) = default;
    block_allocator& operator=(block_allocator&&) = default;
    ~block_allocator() = default;

    /**
     * Constructs a block_allocator with a block_size of _block_size,
     * limiting to block_limit. If block_limit <= 0 then an arbitrary
     * amount of blocks will be allowed
     * @param _block_size The size of each contiguous chunk of memory
     * objects are taken from
     * @param _block_limit The limit on the number of blocks, zero
     * meaning unlimited.
     */
    block_allocator(size_t _block_size, index_type _block_limit = 0);
    
    /**
     * Retrieves a pointer to an location in memory that holds 1 T,
     * except uninitialized. If allocations fails, return nullptr. If
     * actual memory allocations fails, then std::bad_alloc may be thrown
     * @return A pointer pointing to an uninitialized T
     * @raises std::bad_alloc upon failure
     */
    T* get_ptr();

    /**
     * Returns a pointer to an object of type T,
     * initialized with T(...vargs)
     * @param vargs The arguments passed to the constructor
     * @return A pointer to a newly constructed T(...vargs)
     */
    template<class... Args>
    T* create(Args&&... vargs);

    /**
     * Releases the passed pointer, but does not call the destructor
     * of the object. Will invalidate the memory region, however
     * @param to_release The pointer whose memory is being released
     * TODO move nearly-empty blocks around as to minimize searching
     */
    void release(T* to_release);

    /**
     * Destroys the object and releases the memory, similar to delete
     * @param to_free The pointer that is being released
     */
    void free(T* to_free);
};

template<class T, class index_type>
block_allocator<T, index_type>::block_allocator(size_t _block_size,
						index_type _block_limit)
	:
	block_size(_block_size),
	block_limit(_block_limit){
	this->blocks.emplace_back(this->block_size);
    }

template<class T, class index_type>
T* block_allocator<T, index_type>::_get_ptr(){
    //reverse since new blocks are pushed onto the back
    for(index_type i = this->blocks.size(); i > 0;){
	_block& block = this->blocks[--i];
	if (likely(block.first_open)){
	    val_ptr* fst = block.first_open;
	    block.first_open = block.first_open->next;
	    fst->data.block_index = i;
	    return (T*)fst;
	}
    }
    return nullptr;
}

template<class T, class index_type>
T* block_allocator<T, index_type>::get_ptr(){
    T* rpos = this->_get_ptr();
    if(unlikely(not rpos)){
	if(block_limit == 0 || this->blocks.size() <= this->block_limit){
	    this->blocks.emplace_back(this->block_size); 
	    rpos = this->_get_ptr();
	}
    }
    if(not rpos){
	throw std::bad_alloc();
    }
    return rpos;
}

template<class T, class index_type> template<class... Args>
T* block_allocator<T, index_type>::create(Args&&... vargs){
    return new (this->get_ptr()) T(std::forward<Args...>(vargs...));
}

 
template<class T, class index_type>
void block_allocator<T, index_type>::release(T* to_release){
    val_ptr* vptr = (val_ptr*)to_release;
    _block& block = this->blocks[vptr->data.block_index];
    vptr->next = block.first_open;
    block.first_open = vptr;
}

template<class T, class index_type>
void block_allocator<T, index_type>::free(T* free_val){
    free_val->~T();
    this->release(free_val);
}

template<class T, class index_type>
block_allocator<T, index_type>::_block::_block(size_t data_len)
    :
    data(new val_ptr[data_len]){
    for(size_t i = 0; i < data_len-1; i++){
	this->data[i].next = &this->data[i+1];
    }
    this->data[data_len-1].next=nullptr;
    this->first_open = &this->data[0];
}
 
//!A block allocator which has a default size at compile time, \sa block_allocator
template<class T, size_t default_size, class index_type = uint32_t>
class block_allocator_static : public block_allocator<T, index_type>{
public:
    block_allocator_static(size_t block_size = default_size, index_type block_limit = 0)
	:
	block_allocator<T, index_type>(block_size, block_limit){
    }
};

//!allocator with same interface as block_allocator, but calls new/delete
template<class T>
class standard_allocator{
    struct obj_size{
	char data [sizeof(T)];
    };
public:
    //!Allocates an object without initializing it
    T* get_ptr(){
	return (T*)new obj_size;
    }
    //!Creates an object from the given arguments
    template<class... Args>
    T* create(Args&&... args){
	return new (get_ptr()) T(std::forward<Args...>(args...));
    }
    //!Releases the memory at the specified location w/o calling the destructor
    void release(T* data){
	delete (obj_size*)data;
    }
    //!Destroys the object and releases the memory
    void free(T* data){
	data->~T();
	release(data);
    }
    standard_allocator(size_t block_size, size_t block_limit=0){
	//literally just to silence the unused parameter warning
	//please save us optimizer
	block_size=0;
	block_limit=1;
    }
};

} //util
} //openage
#endif
