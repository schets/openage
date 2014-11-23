// Copyright 2014-2014 the openage authors. See copying.md for legal info.
//
#ifndef OPENAGE_UTIL_BLOCK_ALLOCATOR_H_
#define OPENAGE_UTIL_BLOCK_ALLOCATOR_H_
#include <type_traits>
#include <cstddef>
#include <utility>
#include <memory>
#include <vector>
#include <new>
#include <iostream>

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
 */
template<class T, class index_type = uint32_t>
class block_allocator{
protected:
	static_assert(std::is_unsigned<index_type>::value, "index type must be unsigned");
	//!Holds the union of data and next free node
	typedef union val_ptr {
		struct{
			T placeholder;
			index_type block_index;
		} data;
		val_ptr* next;
	} val_ptr;

	using allocator = std::allocator<val_ptr>;

	//!deletion struct for unique_ptr
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
		_block(allocator& _alloc, size_t data_len, val_ptr* hint = (val_ptr*)0);
	};

	template<bool do_free>
	void releaser(T* to_release);

	inline bool is_in(val_ptr* block, val_ptr* test){
		return test >= block & test < (block + this->block_size);
	}
	//!Attempts to retrieve a pointer, returns 0 otherwise.
	T* _get_ptr();

	//!size of each block
	size_t block_size;

	//!list of memory blocks
	std::vector<_block> blocks;

	//!limit on the amount of blocks
	index_type block_limit;

	//!base allocator being used
	allocator alloc;

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
	 * @param a hint on where to begin allocating blocks
	 * meaning unlimited.
	 */
	block_allocator(size_t _block_size, index_type _block_limit = 0, T* hint = 0);

	/**
	 * Retrieves a pointer to an location in memory that holds 1 T,
	 * except uninitialized. If allocations fails, return nullptr. If
	 * actual memory allocations fails, then std::bad_alloc may be thrown
	 * @return A pointer pointing to an uninitialized T
	 */
	T* get_ptr_nothrow();

	/**
	 * The same as get_ptr_nothrow(), except throws std::bad_alloc
	 * when a pointer cannot be retrieved
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
	 * Same as create(args...), except will return a null pointer
	 * instead of throwing if the allocator cannot return another
	 * pointer. However, if more blocks are needed and that allocator
	 * fails, then std::bad_alloc will be thrown
	 */
	template<class... Args>
	T* create_nothrow(Args&&... vargs);

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

	//!Returns the size in bytes of each allocation
	size_t alloc_size() const{
		return sizeof(val_ptr);
	}
};


template<class T, class index_type>
block_allocator<T, index_type>::block_allocator(size_t _block_size,
                                                index_type _block_limit,
                                                T* hint)
	:
	block_size(_block_size),
	block_limit(_block_limit){
	this->blocks.emplace_back(this->alloc, this->block_size, (val_ptr*)hint);
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
	T* rptr = this->get_ptr_nothrow();
	if(unlikely(!rptr)){
		throw std::bad_alloc();
	}
	return rptr;
}

template<class T, class index_type>
T* block_allocator<T, index_type>::get_ptr_nothrow() {
	T* rpos = this->_get_ptr();
	if(unlikely(not rpos)){
		if(block_limit == 0 || this->blocks.size() <= this->block_limit){
			val_ptr* hint = this->blocks.back().data.get()+this->block_size;
			this->blocks.emplace_back(this->alloc, this->block_size, hint);
			rpos = this->_get_ptr();
		}
	}
	return rpos;
}

template<class T, class index_type>
template<class... Args>
T* block_allocator<T, index_type>::create(Args&&... vargs){
	return new (this->get_ptr()) T(std::forward<Args>(vargs)...);
}

template<class T, class index_type>
template<class... Args>
T* block_allocator<T, index_type>::create_nothrow(Args&&... vargs){
	T* space = this->get_ptr_nothrow();
	if(likely(space)){
		return new (space) T(std::forward<Args>(vargs)...);
	}
	return nullptr;
}

template<class T, class index_type>
template<bool do_free>
void block_allocator<T, index_type>::releaser(T* to_release){
	val_ptr* vptr = (val_ptr*)to_release;
	_block& block = this->blocks[vptr->data.block_index];
	if(likely(is_in(block.data.get(), vptr))){
		if(do_free){
			to_release->~T();
		}
		vptr->next = block.first_open;
		block.first_open = vptr;
	}
}

template<class T, class index_type>
void block_allocator<T, index_type>::free(T* free_val){
	this->releaser<true>(free_val);
}

template<class T, class index_type>
void block_allocator<T, index_type>::release(T* free_val) noexcept{
	this->releaser<false>(free_val);
}


template<class T, class index_type>
block_allocator<T, index_type>::_block::_block(allocator& _alloc, size_t data_len, val_ptr* hint)
	:
	alloc(_alloc),
	data(alloc.allocate(data_len, hint), deleter(_alloc, data_len)){
	for(size_t i = 0; i < data_len-1; i++){
		this->data[i].next = &this->data[i+1];
	}
	this->data[data_len-1].next=nullptr;
	this->first_open = &this->data[0];
}

//!allocator with same interface as block_allocator, but standard allocator
template<class T>
class standard_allocator{
	std::allocator<T> alloc;
	T* last_op;
public:
	//!Allocates an object without initializing it
	T* get_ptr(){
		T* rval = alloc.allocate(1, last_op);
		last_op = rval;
		return rval;
	}
	//!Creates an object from the given arguments
	template<class... Args>
	T* create(Args&&... args){
		T* ptr = get_ptr();
		alloc.construct(ptr, std::forward<Args>(args)...);
		return ptr;
	}

	//!Releases the memory at the specified location w/o calling the destructor
	void release(T* data){
		alloc.deallocate(data, 1);
	}
	//!Destroys the object and releases the memory
	void free(T* data){
		alloc.deallocate(data, 1);
		alloc.destroy(data);
	}
	standard_allocator(size_t block_size, size_t block_limit=0, T* hint = 0)
		:
		last_op(hint){
		//literally just to silence the unused parameter warning
		//please save us optimizer
		block_size=0;
		block_limit=1;
	}
};



/**
 * This is a allocator for single allocations that returns memory out
 * of a pre-allocated blocks instead of allocating memory on each call
 * to new. This makes  it fairly cheap to allocate many small objects,
 * and also greatly improves memory locality.
 *
 * The fixed block alocator will only allocate out of a isngle block,
 * as opposed to a standard block allocator. This improves performance
 * and memory usage/locality, but strictly enforces limited memory usage
 *
 * Object allocated by this do not live longer than the duration of
 * the allocator. However, destructors must be called manually as with delete
 * if they perform meaningful work
 *
 *
 */
template<class T>
class fixed_block_allocator{
protected:
	//!Holds the union of data and next free node
	typedef union val_ptr {
		T placeholder;
		val_ptr* next;
	} val_ptr;

	using allocator = std::allocator<val_ptr>;

	//!deletion struct for unique_ptr
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

	template<bool do_release>
	void releaser(T* to_release);
	
	inline bool is_in(val_ptr* block, val_ptr* test){
		return test >= block & test < (block + this->block_size);
	}
	//!Attempts to retrieve a pointer, returns 0 otherwise.
	T* _get_ptr();

	//!size of each block
	size_t block_size;

	//!base allocator being used
	allocator alloc;

	using ptr_type = std::unique_ptr<val_ptr[], deleter>;
    //! A pointer to the data being held
	ptr_type data;

	//! A pointer to the first open value
	val_ptr* first_open;

public:
	fixed_block_allocator(const fixed_block_allocator&) = delete;
	fixed_block_allocator operator=(const fixed_block_allocator&) = delete;
    
	fixed_block_allocator(fixed_block_allocator&&) = default;
	fixed_block_allocator& operator=(fixed_block_allocator&&) = default;
	~fixed_block_allocator() = default;

	/**
	 * Constructs a fixed_block_allocator with a block_size of _block_size,
	 * @param _block_size The numer of objects in the block
	 * @param a hint on where to begin allocating blocks
	 */
	fixed_block_allocator(size_t _block_size, T* hint = 0);

	/**
	 * Retrieves a pointer to an location in memory that holds 1 T,
	 * except uninitialized. If allocations fails, return nullptr.
	 * @return A pointer pointing to an uninitialized T
	 */
	T* get_ptr_nothrow() noexcept;

	/**
	 * The same as get_ptr_nothrow(), except throws std::bad_alloc
	 * when a pointer cannot be retrieved
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
	 * Same as create(args...), except will return a null pointer
	 * instead of throwing if the allocator cannot return another
	 * pointer. However, if more blocks are needed and that allocator
	 * fails, then std::bad_alloc will be thrown
	 */
	template<class... Args>
	T* create_nothrow(Args&&... vargs);

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

	//!Returns the size in bytes of each allocation
	size_t alloc_size() const{
		return sizeof(val_ptr);
	}
};


template<class T>
fixed_block_allocator<T>::fixed_block_allocator(size_t _block_size,
                                                T* hint)
	:
	block_size(_block_size),
	alloc(),
	data(alloc.allocate(block_size, hint),
	     deleter(alloc, block_size)),
	first_open(data.get()){

	for(size_t i = 0; i < this->block_size-1; i++){
		this->data[i].next = &this->data[i+1];
	}
	this->data[this->block_size-1].next = nullptr;
}

template<class T>
T* fixed_block_allocator<T>::get_ptr_nothrow() noexcept {
	//reverse since new blocks are pushed onto the back
	if(likely(this->first_open)){
		val_ptr* fst = this->first_open;
		this->first_open = this->first_open->next;
		return (T*)fst;
	}
	return nullptr;
}
 
template<class T>
T* fixed_block_allocator<T>::get_ptr(){
	T* rptr = this->get_ptr_nothrow();
	if(unlikely(!rptr)){
		throw std::bad_alloc();
	}
	return rptr;
}

template<class T>
template<class... Args>
T* fixed_block_allocator<T>::create(Args&&... vargs){
	return new (this->get_ptr()) T(std::forward<Args>(vargs)...);
}

template<class T>
template<class... Args>
T* fixed_block_allocator<T>::create_nothrow(Args&&... vargs){
	T* space = this->get_ptr_nothrow();
	if(likely(space)){
		return new (space) T(std::forward<Args>(vargs)...);
	}
	return nullptr;
}

template<class T>
template<bool do_release>
void fixed_block_allocator<T>::releaser(T* to_release) {
	val_ptr* vptr = (val_ptr*)to_release;
	if(likely(is_in(this->data.get(), vptr))){
		if(do_release){
			to_release->~T();
		}
		vptr->next = this->first_open;
		this->first_open = vptr;
	}
}

template<class T>
void fixed_block_allocator<T>::free(T* free_val){
	this->releaser<true>(free_val);
}

template<class T>
void fixed_block_allocator<T>::release(T* free_val) noexcept{
	this->releaser<false>(free_val);
}

} //namespace util
} //namespace openage
#endif
