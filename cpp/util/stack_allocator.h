// Copyright 2014-2014 the openage authors. See copying.md for legal info.
//
#ifndef OPENAGE_UTIL_STACK_ALLOCATOR_H_
#define OPENAGE_UTIL_STACK_ALLOCATOR_H_
#include <memory>
#include <vector>

#include "compiler.h"
namespace openage{
namespace util{
    
//TODO: Documentation

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

	template<bool do_release>
	void releaser();

	using ptr_type = std::unique_ptr<T[], deleter>;
	std::vector<ptr_type> ptrs;
	size_t stack_limit;
	size_t stack_size;
	T* cur_ptr;
	T* cur_stackend;
	typename std::vector<ptr_type>::iterator cur_substack;
	bool add_substack(T* hint = 0);
	allocator alloc;

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
	stack_size(_stack_size){
	this->add_substack(hint);
}

template<class T>
bool stack_allocator<T>::add_substack(T* hint){
	if(unlikely(this->stack_limit && this->ptrs.size() == this->stack_limit)){
		return false; //no more 'memory' available
	}
	else{
		if(!hint){
			hint = this->ptrs.back().get() + this->stack_size;
		}
		T* space = this->alloc.allocate(this->stack_size, hint);
		this->ptrs.emplace_back(space, deleter(this->alloc, this->stack_size));
		this->cur_ptr = space;	
		this->cur_stackend = space + this->stack_size;
		this->cur_substack = this->ptrs.end()-1;
		return true;
	}
}

template<class T>
T* stack_allocator<T>::get_ptr_nothrow(){
	if(unlikely(this->cur_ptr == this->cur_stackend)){
		++(this->cur_substack);
		if(this->cur_substack == ptrs.end()){
			if(!this->add_substack()){
				return nullptr;
			}
		}
		else{
			this->cur_ptr = this->cur_substack->get();
			this->cur_stackend = this->cur_ptr + this->stack_size;
		}
	}
	return this->cur_ptr++;
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
template<bool do_release>
void stack_allocator<T>::releaser(){	
	if(unlikely(this->cur_ptr == this->cur_substack->get())){
		if(unlikely(this->cur_substack == this->ptrs.begin())){
			return;
		}
		--(this->cur_substack);
		this->cur_stackend = this->cur_substack->get() + this->stack_size;
		this->cur_ptr = this->cur_stackend - 1;
	}
	else{
		this->cur_ptr--;
	}
	if(do_release){
		this->cur_ptr->~T();
	}
}

template<class T>
void stack_allocator<T>::free(){
	this->releaser<true>();
}

template<class T>
void stack_allocator<T>::release() noexcept {
	this->releaser<false>();
}

/**
 * This class emulates a stack, except in dynamic memory.
 * It only holds a single block of memory, and will
 * fail to allocate more once the limit has been reached
 */

template<class T>
class fixed_stack_allocator{

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

	//does actual release, decision done at compile time
	template<bool do_free>
	void releaser();

	using ptr_type = std::unique_ptr<T[], deleter>;
	allocator alloc;
	ptr_type data;
	T* cur_ptr;
	T* cur_stackend;

public:
	fixed_stack_allocator(const fixed_stack_allocator&) = delete;
	fixed_stack_allocator& operator=(const fixed_stack_allocator&) = delete;

	fixed_stack_allocator(fixed_stack_allocator&&) = default;
	fixed_stack_allocator& operator=(fixed_stack_allocator&&) = default;
	~fixed_stack_allocator() = default;

	/**
	 * Constructs a stack allocator.
	 * Each substack contains stack_size elements,
	 * and there can only be stack_limit elements
	 * or unlimited if stack_limit==0
	 * @param stack_size The size of each sub-stack used in allocating
	 * @param hint A hint for where to do the next allocation
	 */
	fixed_stack_allocator(size_t stack_size, T* hint=0);

	/**
	 * Retrieves a pointer to an location in memory that holds 1 T,
	 * except uninitialized. If allocations fails, return nullptr.
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
	 * instead of throwing if the allocator cannot allocate memory
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
fixed_stack_allocator<T>::fixed_stack_allocator(size_t stack_size, T* hint)
	:
	alloc(),
	data(alloc.allocate(stack_size, hint),
	     deleter(alloc, stack_size)),
	cur_ptr(data.get()),
	cur_stackend(cur_ptr + stack_size){
}

template<class T>
T* fixed_stack_allocator<T>::get_ptr_nothrow() noexcept{
	if(likely(this->cur_ptr != this->cur_stackend)){
		return this->cur_ptr++;
	}
	return nullptr;
}

template<class T>
T* fixed_stack_allocator<T>::get_ptr(){
	T* rptr = get_ptr_nothrow();
	if(unlikely(!rptr)){
		throw std::bad_alloc();
	}
	return rptr;
}

template<class T>
template<class... Args>
T* fixed_stack_allocator<T>::create(Args&&... vargs){
	return new (this->get_ptr()) T(std::forward<Args>(vargs)...);
}

template<class T>
template<class... Args>
T* fixed_stack_allocator<T>::create_nothrow(Args&&... vargs){
	T* rptr = this->get_ptr_nothrow();
	if(unlikely(not rptr)){
		throw std::bad_alloc();
	}
	return new (this->get_ptr_nothrow()) T(std::forward<Args>(vargs)...);
}

template<class T>
template<bool do_release>
void fixed_stack_allocator<T>::releaser(){	
	if(likely(this->cur_ptr != this->data.get())){
		this->cur_ptr--;
		if(do_release){
			this->cur_ptr->~T();
		}
	}
}

template<class T>
void fixed_stack_allocator<T>::free(){
	this->releaser<true>();
}

template<class T>
void fixed_stack_allocator<T>::release() noexcept{
	this->releaser<false>();
}

} //namespace util
} //namespace openage
#endif
