// Copyright 2014-2014 the openage authors. See copying.md for legal info.
//
#ifndef OPENAGE_UTIL_STL_BLOCK_ALLOCATOR_H_
#define OPENAGE_UTIL_STL_BLOCK_ALLOCATOR_H_
#include <algorithm>
#include <utility>
#include <memory>
#include <vector>
#include <new>

#include "compiler.h"
namespace openage{
namespace util{
/**
 * This is an allocator that is similar to block_allocator, with a few
 * differences.
 *     1. This allocator supports allocating blocks of memory
 *
 *     2. This allocator supports the STL allocator inferface
 *
 *     3. This allocator has a more expensive free operation
 * log(num_blocks), bigger constant factor as well (stl_block_allocator is
 * a very quick O(1))
 *
 *     4. The allocated blocks don't take up extra memory with an index
 */
template<class T>
class stl_block_allocator{
protected:
    //!Holds the union of data and next free node
    typedef union val_ptr {
	T value;//for now, this ensures sizeof(ptr).
	val_ptr* next;
    } val_ptr;

    using allocator = std::allocator<val_ptr>;
    struct deleter{
	allocator& alloc;
	size_t num;
	deleter(allocator& _alloc, size_t val)
	    :
	    alloc(_alloc),
	    num(val){
	}
	void operator()(val_ptr* del) const{
	    alloc.deallocate(del, num);
	}
    };
    //!struct that contains the memory being allocated
    struct _block{
	allocator& alloc;
	std::unique_ptr<val_ptr[], deleter> data;
	val_ptr* first_open;
	val_ptr* last_open;
	size_t num_open;
	_block(allocator& _alloc, size_t data_len, val_ptr* hint = (val_ptr*)0);
    };

    //!Attempts to retrieve a pointer, returns 0 otherwise.
    T* _get_ptr();

    //!releases data from the specified block, NO CHECKS ARE DONE
    void release_from(val_ptr* vptrp, _block* block) noexcept;

    //!returns true if a pointer is in a range
    inline bool is_in(val_ptr* test, val_ptr* data){
	return test >= data && test < data + block_size;
    }
    //!Finds the block that the pointer came from
    _block* find_parent(val_ptr* vptr);

    //!adds a new data pointer to the group
    void add_new_data(_block* bptr);

    //!gets a pointer of many values
    T* get_many(size_t num);

    //!Returns a pointer pointing to the next open block, or nullptr
    val_ptr* check_open_block(_block* block, size_t num);

    //!size of each block
    size_t block_size;

    //!list of memory blocks
    std::vector<_block> blocks;

    //!vector of the pointers that each block contains
    std::vector<_block*> block_ptrs;

    //!base allocator being used
    allocator alloc;

public:
    stl_block_allocator(const stl_block_allocator&) = delete;
    stl_block_allocator operator=(const stl_block_allocator&) = delete;
    
    stl_block_allocator(stl_block_allocator&&) = default;
    stl_block_allocator& operator=(stl_block_allocator&&) = default;
    ~stl_block_allocator() = default;

    /**
     * Constructs a stl_block_allocator with a block_size of _block_size,
     * limiting to block_limit. If block_limit <= 0 then an arbitrary
     * amount of blocks will be allowed
     * @param _block_size The size of each contiguous chunk of memory
     * objects are taken from
     * @param _block_limit The limit on the number of blocks, zero
     * @param a hint on where to begin allocating blocks
     * meaning unlimited.
     */
    stl_block_allocator(size_t _block_size, T* hint = 0);

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
    void release(T* to_release) noexcept;

    /**
     * Destroys the object and releases the memory, similar to delete
     * @param to_free The pointer that is being released
     */
    void free(T* to_free);

    //stl functions

    //!TODO make algorithm respect hint!
    T* allocate(size_t num, T* hint);

    void deallocate(T* data, size_t num);

    template<class... Args>
    void construct(T* data, Args&&... vargs);

    void destroy(T* data){data->~T();}

    T* address(T& val) noexcept {return &val;}
    const T* address(const T& val) noexcept {return &val;}
    
    size_t max_size() const noexcept{return this->block_size;}
};

template<class T>
stl_block_allocator<T>::stl_block_allocator(size_t _block_size,
							T* hint)
    :
    block_size(_block_size){
    this->blocks.emplace_back(this->alloc, this->block_size, (val_ptr*)hint);
    this->block_ptrs.emplace_back(&this->blocks[0]);
}

template<class T>
T* stl_block_allocator<T>::_get_ptr(){
    //reverse since new blocks are pushed onto the back
    for(auto block=blocks.rbegin(); block != blocks.rend(); block++){
	if (likely(block->first_open)){
	    val_ptr* fst = block->first_open;
	    block->first_open = block->first_open->next;
	    if(unlikely(!block->first_open)){
		block->last_open = nullptr;
	    }
	    return (T*)fst;
	}
    }
    return nullptr;
}

template<class T>
T* stl_block_allocator<T>::get_ptr(){
    T* rpos = this->_get_ptr();
    if(unlikely(not rpos)){
	this->blocks.emplace_back(this->alloc, this->block_size,
				  this->blocks.back().data.get()); 
	this->add_new_data(&this->blocks.back());
	rpos = this->_get_ptr();
    }
    if(not rpos){
	throw std::bad_alloc();
    }
    return rpos;
}

template<class T>
template<class... Args>
T* stl_block_allocator<T>::create(Args&&... vargs){
    return new (this->get_ptr()) T(std::forward<Args>(vargs)...);
}

template<class T>
void stl_block_allocator<T>::release_from(val_ptr* vptr, _block* block) noexcept{
    if(unlikely(!block.first_open)){
	block.first_open = block.last_open = vptr;
	vptr->next=nullptr;
    }
    else if(vptr > block.last_open){
	block.last_open->next = vptr;
	block.last_open=vptr;
    }
    else if(vptr < block.first_open){
	vptr->next = block.first_open;
	block.first_open = vptr;
    }
    else{
	val_ptr* cur = block->first_open;
	while(cur->next){
	    if(vptr < cur->next){	
		vptr->next = cur->next;
		cur->next=vptr;
		return;
	    }
	    cur = cur->next;
	}
	cur->next = vptr;
	vptr->next = nullptr;
    }
}

template<class T>
void stl_block_allocator<T>::release(T* to_release) noexcept{
    val_ptr* vptr = (val_ptr*)to_release;
    release_from(vptr, find_parent(vptr));
}

template<class T>
void stl_block_allocator<T>::free(T* free_val){
    free_val->~T();
    this->release(free_val);
}

template<class T>
_block* stl_block_allocator<T>::find_parent(val_ptr* p){
    size_t blocks_len = this->blocks.size();
    if(unlikely(blocks_len == 0 ||
		p < block_ptrs.front() ||
		p > block_ptrs.back() + block_size)){
	return nullptr;
    }

    if(blocks_len == 1){
	return block_ptrs.front();
    }

    auto compare_fnc = [](const val_ptr* v1, const _block* block){
	return v1 < block->block->data.get();
    }
    auto upper_bound = std::upper_bound(block_ptrs.begin(), block_ptrs.end(),
					p, compare_fnc);
    _block* block = *(--upper_bound);

    if(unlikely(!this->is_in(p, upper_bound))){
	return nullptr;
    }
    return block;
}

template<class T>
void stl_block_allocator<T>::add_new_data(_block* p){
    auto pos = std::lower_bound(block_ptrs.begin(), block_ptrs.end(), p);
    block_ptrs.insert(pos, p);
}

//DOES NOT WORK
template<class T>
val_ptr* stl_block_allocator<T>::check_open_block(_block* block, size_t num){

    if(unlikely(num > num_open)){
	return nullptr;
    } 
    val_ptr* vptr = block->first_open;
    if(unlikely(not vptr)){
	return nullptr;
    } 

    while(likely(vptr->next)){
	if(vptr->next - vptr > (num - 1)){ //num > 0
	    return vptr;
	}
	vptr = vptr->next;
    }

    //find distance between vptr, and distance till end
    size_t dat_index = (size_t)(vptr - block->data.get());
    
    return nullptr;
}

template<class T>
T* get_many(size_t num){
    for(_block& block : blocks){
	val_ptr* test = check_open_block(&block, num);
	if(test){
	    return test;
	}
    }
}
    
template<class T>
stl_block_allocator<T>::_block::_block(allocator& _alloc, size_t data_len, val_ptr* hint)
    :
    alloc(_alloc),
    data(alloc.allocate(data_len, hint), deleter(_alloc, data_len)){
    for(size_t i = 0; i < data_len-1; i++){
	this->data[i].next = &this->data[i+1];
    }
    this->data[data_len-1].next=nullptr;
    this->first_open = &this->data[0];
    this->last_open = &data[data_len-1];
}

template<class T>
T* stl_block_allocator<T>::allocate(size_t num, T* hint){
    if(unlikely(!num)){
	return nullptr;
    }
    else if(num == 1){
	return this->get_ptr();
    }
    return this->get_many(num);
}

} //util
} //openage
#endif
