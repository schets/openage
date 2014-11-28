// Copyright 2014-2014 the openage authors. See copying.md for legal info.
//
#ifndef OPENAGE_UTIL_BLOCK_ALLOCATOR_H_
#define OPENAGE_UTIL_BLOCK_ALLOCATOR_H_
#include <cstdlib>
#include <utility>
#include <memory>
#include <vector>
#include <list>
#include <new>

#include "compiler.h"
namespace openage{
namespace util{
namespace alloc{ //push implementation into 'hidden' namespace

/**
 * Class for common block allocator functionality
 * Base implementation must provide get_ptr(),
 * and template<bool> releaser(T*), which releases and frees
 * if the template parameter is true. Also must make a type
 * val_ptr available, which is the type that
 * each allocation block refers too
 */
template<class T, template<class> class core_alloc>
class block_allocator_common : public core_alloc<T>{

	using val_ptr = typename core_alloc<T>::val_ptr;
	using this_type = block_allocator_common<T, core_alloc>;

public:
	
	~block_allocator_common() = default;
	//base classes have whatever damn constructor they want
	template<class... Args>
	block_allocator_common(Args&&... vargs)
		:
		core_alloc<T>(std::forward<Args>(vargs)...){
	}
	
	//!deletion struct for unique_ptr/shared_ptr
	struct deleter{
		this_type& alloc;
		inline void operator()(T* to_del){
			alloc.free(to_del);
		}
		deleter(this_type& all)
			:
			alloc(all){}
	};
	using unique_type = std::unique_ptr<T, deleter>;
	using shared_type = std::shared_ptr<T>;

	/**
	 * The same as get_ptr_nothrow(), except throws std::bad_alloc
	 * when a pointer cannot be retrieved
	 * @raises std::bad_alloc upon failure
	 */
	inline T* get_ptr_throw(){
		T* space = this->get_ptr();
		if(unlikely(!space)){
			throw std::bad_alloc();
		}
		return space;
	}

	/**
	 * Returns a pointer to an object of type T,
	 * initialized with T(...vargs)
	 * @param vargs The arguments passed to the constructor
	 * @return A pointer to a newly constructed T(...vargs)
	 */
	template<class... Args>
	inline T* create(Args&&... vargs){
		return new(this->get_ptr()) T(std::forward<Args>(vargs)...);
	}

	/**
	 * Same as create(args...), except will throw std::bad_alloc
	 * instead of returning nullptr if the allocator cannot return another
	 * pointer.
	 */
	template<class... Args>
	inline T* create_throw(Args&&... vargs){
		return new(this->get_ptr_throw()) T(std::forward<Args>(vargs)...);
	}
		
	/**
	 * Releases the passed pointer, but does not call the destructor
	 * of the object. Will invalidate the memory region, however
	 * @param to_release The pointer whose memory is being released
	 */
	inline void release(T* to_release) {
		this->template releaser<false>(to_release);
	}

	/**
	 * Releases the passed pointer, and calls the destructor
	 * @param to_release The pointer whose memory is being released
	 */
	inline void free(T* to_release){
		this->template releaser<true>(to_release);
	}

	//!same as create, except returns a unique_pointer
	template<class... Args>
	inline unique_type make_unique(Args&&... vargs){
		return std::unique_ptr<T, deleter>(create(std::forward<Args>(vargs)...), get_deleter());
	}

	//!The equivilant of create_throw for unique_pointers
	template<class... Args>
	inline unique_type make_unique_throw(Args&&... vargs){
		return std::unique_ptr<T, deleter>(create_throw(std::forward<Args>(vargs)...), get_deleter());
	}

	//!same as create, except returns a shared pointer
	template<class... Args>
	inline shared_type make_shared(Args&&... vargs){
		return std::shared_ptr<T>(create(std::forward<Args>(vargs)...), get_deleter());
	}

	//!The equivilant of create_throw for shared pointers
	template<class... Args>
	inline shared_type make_shared_throw(Args&&... vargs){
		return std::shared_ptr<T>(create_throw(std::forward<Args>(vargs)...), get_deleter());
	}

	inline size_t alloc_size() const{return sizeof(val_ptr);}
	inline deleter get_deleter()  {return *this;}
};

template<class T>
class block_allocator{
protected:
	using this_type = block_allocator<T>;
	struct _block;
	//!Holds the union of data and next free node
	typedef union val_ptr {
		struct{
			T placeholder;
			_block* holder;
		} data;
		val_ptr* next;
	} val_ptr;

	//!deletion struct for unique_ptr
	struct internal_deleter{
		void operator()(val_ptr* del) const{
			::free(del);
		}
	};

	//!struct that contains the memory being allocated
	struct _block{
		std::unique_ptr<val_ptr[], internal_deleter> data;
		val_ptr* first_open;
		size_t ptr_ind;
		_block(size_t data_len, size_t ind);
	};

	template<bool do_free>
	inline void releaser(T* to_release);

	void update_blocks(_block* block) ;

	void add_data() ;

	void swap_good() ;

	//!size of each block
	size_t block_size;

	//!block lookup vector
	std::vector<std::unique_ptr<_block> > lookup_vec;

	//!location of the last good data block
	size_t last_good;


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
	 */
	block_allocator(size_t _block_size);

	/**
	 * Retrieves a pointer to an location in memory that holds 1 T,
	 * except uninitialized. If allocations fails, return nullptr.
	 * @return A pointer pointing to an uninitialized T
	 */
	T* get_ptr();
};

template<class T>
block_allocator<T>::block_allocator(size_t _block_size)
	:
	block_size(_block_size){
	this->add_data();
	this->last_good=0;
}

template<class T>
void block_allocator<T>::add_data(){
	_block* new_b = new (std::nothrow) _block(this->block_size,
	                           this->lookup_vec.size());
	if(likely(new_b)){
		if(unlikely(!new_b->data.get())){
			delete new_b;
		} 
		this->lookup_vec.emplace_back(new_b);
		this->last_good++;
	}
}

template<class T>
void  block_allocator<T>::swap_good() {
	std::swap(this->lookup_vec.back(),
	          this->lookup_vec[last_good]);
	
	std::swap(this->lookup_vec.back()->ptr_ind,
	          this->lookup_vec[last_good]->ptr_ind);

	this->last_good++;
}

template<class T>
T* block_allocator<T>::get_ptr() {
	_block* block = lookup_vec.back().get();
	val_ptr* fst = block->first_open;
	if(unlikely(!fst)){return nullptr;}

	block->first_open = block->first_open->next;
	fst->data.holder = block;
	if(unlikely(!block->first_open)){
		if(last_good != lookup_vec.size()-1){
			this->swap_good();
		}
		else{
			this->add_data();
		}
	}
	return (T*)fst;
}

template<class T>
void block_allocator<T>::update_blocks(_block* block) {
	size_t ind = block->ptr_ind;
	this->last_good--;
	std::swap(this->lookup_vec[this->last_good], this->lookup_vec[ind]);
	this->lookup_vec[this->last_good]->ptr_ind = this->last_good;
	this->lookup_vec[ind]->ptr_ind = ind;
}

template<class T>
template<bool do_free>
inline void block_allocator<T>::releaser(T* to_release){
	val_ptr* vptr = (val_ptr*)to_release;
	_block* block = vptr->data.holder;
	if(do_free){
		to_release->~T();
	}
	if(unlikely(!block->first_open)){
		this->update_blocks(block);
	}
	vptr->next = block->first_open;
	block->first_open = vptr;
}

template<class T>
block_allocator<T>::_block::_block(size_t data_len, size_t ind)
	:
	data((val_ptr*)malloc(data_len*sizeof(val_ptr)), internal_deleter()),
	ptr_ind(ind){

	if(likely(data)){
		for(size_t i = 0; i < data_len-1; i++){
			this->data[i].next = &this->data[i+1];
		}
		this->data[data_len-1].next=nullptr;
		this->first_open = &this->data[0];
	}
	else{
		this->first_open = nullptr;
	}
}



//!fixed allocator implementation
template<class T>
class fixed_block_allocator{
protected:
	using this_type = fixed_block_allocator<T>;
	//!Holds the union of data and next free node
	typedef union val_ptr {
		T placeholder;
		val_ptr* next;
	} val_ptr;

	//!deletion struct for unique_ptr
	struct internal_deleter{
		void operator()(val_ptr* del) const{
			::free(del);
		}
	};

	template<bool do_free>
	void releaser(T* to_release);
	
	//!Attempts to retrieve a pointer, returns 0 otherwise.
	T* _get_ptr();

	//!size of each block
	size_t block_size;

	using ptr_type = std::unique_ptr<val_ptr[], internal_deleter>;
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
	 */
	fixed_block_allocator(size_t _block_size);

	/**
	 * Retrieves a pointer to an location in memory that holds 1 T,
	 * except uninitialized. If allocations fails, return nullptr.
	 * @return A pointer pointing to an uninitialized T
	 */
	T* get_ptr() ;
};


template<class T>
fixed_block_allocator<T>::fixed_block_allocator(size_t _block_size)
	:
	block_size(_block_size),
	data((val_ptr*)malloc(block_size*sizeof(val_ptr)), internal_deleter()),
	first_open(data.get()){

	for(size_t i = 0; i < this->block_size-1; i++){
		this->data[i].next = &this->data[i+1];
	}
	this->data[this->block_size-1].next = nullptr;
}

template<class T>
T* fixed_block_allocator<T>::get_ptr()  {
	//reverse since new blocks are pushed onto the back
	if(likely(this->first_open)){
		val_ptr* fst = this->first_open;
		this->first_open = this->first_open->next;
		return (T*)fst;
	}
	return nullptr;
}
 
template<class T>
template<bool do_free>
void fixed_block_allocator<T>::releaser(T* to_release){
	val_ptr* vptr = (val_ptr*)to_release;
	if(do_free){
		to_release->~T();
	}
	vptr->next = this->first_open;
	this->first_open = vptr;
}


//!allocator with same interface as block_allocator, but standard allocator
template<class T>
class standard_allocator{
protected:
	template<bool do_free>
	inline void releaser(T* to_release){
		if(do_free){to_release->~T();}
		free(to_release);
	}

	//lol not really but close enough.
	//If there is a portable way of finding out
	//how much malloc returns let me know
	using val_ptr = T;

public:
	//!Allocates an object without initializing it
	inline T* get_ptr(){
		return malloc(sizeof(T));
	}

	standard_allocator(size_t block_size){
		block_size=0;
	}
};


} //namespace alloc

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
 * set on the number of blocks. The maximum number of blocks is 2^32
 *
 */
template<class T>
using block_allocator =
	alloc::block_allocator_common<T, alloc::block_allocator>;

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
 */
template<class T>
using fixed_block_allocator =
	alloc::block_allocator_common<T, alloc::fixed_block_allocator>;

//!'Block allocator' which just calls malloc under the hood
template<class T>
using standard_allocator =
	alloc::block_allocator_common<T, alloc::standard_allocator>;


} //namespace util
} //namespace openage
#endif
