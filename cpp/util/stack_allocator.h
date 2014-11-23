// Copyright 2014-2014 the openage authors. See copying.md for legal info.
//
#ifndef OPENAGE_UTIL_STACK_ALLOCATOR_H_
#define OPENAGE_UTIL_STACK_ALLOCATOR_H_
#include <memory>
#include <vector>

#include "compiler.h"
namespace openage{
namespace util{

/**
 * This class emulates a stack, except in dynamic memory.
 * It will grow to accomodate the amount of memory needed,
 * and provides almost free allocations/deallocations.
 * However, memory can only be acquired/released from the top
 * of the stack
 */
template<class T>
class stack_allocator{
	using allocator = std::allocator<T>;
	struct deleter{
		allocator& alloc;
		size_t num;
		deleter(allocator& _alloc, size_t val)
			:
			alloc(_alloc),
			num(val){
		}
		void operator()(T* del) const{
			alloc.deallocate(del, num);
		}
	};

	using ptr_type = std::unique_ptr<T[], deleter>;
	std::vector<ptr_type> ptrs;
	size_t stack_limit;
	size_t stack_size;
	size_t stack_pos;
	size_t stack_ind;
	bool add_substack(T* hint = 0);
	allocator alloc;

	template<bool do_free> //compile time for effeciency
	void _release();
public:
	stack_allocator(const stack_allocator&) = delete;
	stack_allocator& operator=(const stack_allocator&) = delete;

	stack_allocator(stack_allocator&&) = default;
	stack_allocator& operator=(stack_allocator&&) = default;
	~stack_allocator() = default;

	/**
	 * Constructs a stack allocator.
	 * Each substack contains stack_size elements,
	 * and there can only be stack_limit elements
	 * or unlimited if stack_limit==0
	 * @param stack_size The size of each sub-stack used in allocating
	 * @param stack_limit The limit n the number of sub stacks
	 * @param hint A hint for where to do the next allocation
	 */
	stack_allocator(size_t stack_size, size_t stack_limit=0, T* hint=0);

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
	 * Releases the pointer on the top of the stack,
	 * but does not call the destructor of the object.
	 */
	void release() noexcept;

	/**
	 * Destroys the object on the top of the stack and releases the
	 * memory, similar to delete
	 */
	void free();

};

template<class T>
stack_allocator<T>::stack_allocator(size_t _stack_size, size_t _stack_limit, T* hint)
	:
	stack_limit(_stack_limit),
	stack_size(_stack_size),
	stack_pos(0){
	this->add_substack(hint);
	this->stack_ind=0;
}

template<class T>
bool stack_allocator<T>::add_substack(T* hint){
    
	if(unlikely(this->stack_limit && this->stack_ind == this->stack_limit)){
		return false;
	}
	else{
		if(!hint){
			hint = this->ptrs.back().get() + this->stack_size;
		}
		T* space = this->alloc.allocate(this->stack_size, hint);
		this->ptrs.emplace_back(space, deleter(this->alloc, this->stack_size));
		this->stack_ind++;
		this->stack_pos=0;
		return true;
	}
}
template<class T>
T* stack_allocator<T>::get_ptr_nothrow(){
	if(unlikely(this->stack_pos == this->stack_size)){
		if(this->stack_ind == ptrs.size()-1){
			if(!this->add_substack()){
				return nullptr;
			}
		}
		else{
			this->stack_ind++;
			this->stack_pos=0;
		}
	}
	//TODO: explicitly track stack pointer? overoptimizing?
	return &(this->ptrs[this->stack_ind][this->stack_pos++]);
}

template<class T>
T* stack_allocator<T>::get_ptr(){
	T* rptr = get_ptr_nothrow();
	if(unlikely(!rptr)){
		throw std::bad_alloc();
	}
	return rptr;
}

template<class T>
template<class... Args>
T* stack_allocator<T>::create(Args&&... vargs){
	return new (this->get_ptr()) T(std::forward<Args>(vargs)...);
}

template<class T>
template<class... Args>
T* stack_allocator<T>::create_nothrow(Args&&... vargs){
	return new (this->get_ptr_nothrow()) T(std::forward<Args>(vargs)...);
}


template<class T>
template<bool do_free>
void stack_allocator<T>::_release(){	
	if(unlikely(this->stack_pos == 0)){
		if(unlikely(this->stack_ind == 0)){
			return;
		}
		this->stack_pos = this->stack_size - 1;
		this->stack_ind--;
	}
	else{
		this->stack_pos--;
	}
	if(do_free){//decided at compile time, no actual branch
		this->ptrs[this->stack_ind][this->stack_pos].~T();
	}
}

template<class T>
void stack_allocator<T>::free(){
	this->_release<true>();
}

template<class T>
void stack_allocator<T>::release() noexcept{
	this->_release<false>();    
}

} //namespace util
} //namespace openage
#endif
