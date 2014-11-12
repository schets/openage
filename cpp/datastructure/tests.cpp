// Copyright 2014-2014 the openage authors. See copying.md for legal info.

#include "../log.h"
#include "../datastructure/doubly_linked_list.h"
#include "../datastructure/pairing_heap.h"

namespace openage {
namespace datastructure {
namespace tests {

void pairing_heap() {
	PairingHeap<int> heap;
	int stage = 0;

	if (not (heap.size() == 0)) { goto out; }
	stage += 1;

	heap.push(0);
	heap.push(1);
	heap.push(2);
	heap.push(3);
	heap.push(4);
	stage += 1;

	// 01234
	if (not (heap.size() == 5)) { goto out; }
	if (not (heap.top() == 0)) { goto out; }
	stage += 1;

	if (not (0 == heap.pop())) { goto out; }
	if (not (1 == heap.pop())) { goto out; }
	if (not (2 == heap.pop())) { goto out; }
	if (not (3 == heap.pop())) { goto out; }
	stage += 1;

	// 4
	if (not (heap.size() == 1)) { goto out; }
	stage += 1;

	heap.push(0);
	heap.push(10);

	stage += 1;

	if (not (0 == heap.pop())) { goto out; }
	if (not (4 == heap.pop())) { goto out; }
	if (not (10 == heap.pop())) { goto out; }
	if (not (heap.size() == 0)) { goto out; }
	stage += 1;

	heap.push(5);
	heap.push(5);
	heap.push(0);
	heap.push(5);
	heap.push(5);
	if (not (0 == heap.pop())) { goto out; }
	if (not (5 == heap.pop())) { goto out; }
	if (not (5 == heap.pop())) { goto out; }
	if (not (5 == heap.pop())) { goto out; }
	if (not (5 == heap.pop())) { goto out; }

	if (not (heap.size() == 0)) { goto out; }
	stage += 1;

	return;
out:
	log::err("pairing heap test failed at stage %d", stage);
	throw "failed pairing heap test";
}

void doubly_linked_list() {
	datastructure::DoublyLinkedList<int> list;
	int stage = 0;

	if (not list.empty()) { goto out; }

	list.push_front(0);
	list.push_front(1);
	list.push_front(2);

	list.push_back(3);
	list.push_front(4);
	list.push_back(5);
	stage += 1;

	// 421035
	if (not (list.size() == 6)) { goto out; }
	stage += 1;

	if (not (5 == list.pop_back()))  { goto out; }
	if (not (4 == list.pop_front())) { goto out; }
	stage += 1;

	// 2103
	if (not (list.size() == 4)) { goto out; }
	stage += 1;

	list.push_back(6);
	list.push_front(7);
	list.push_back(8);
	stage += 1;

	// 7210368
	if (not (8 == list.pop_back()))  { goto out; } stage += 1;
	if (not (6 == list.pop_back()))  { goto out; } stage += 1;
	if (not (3 == list.pop_back()))  { goto out; } stage += 1;
	if (not (7 == list.pop_front())) { goto out; } stage += 1;
	if (not (2 == list.pop_front())) { goto out; } stage += 1;
	if (not (0 == list.pop_back()))  { goto out; } stage += 1;
	if (not (1 == list.pop_back()))  { goto out; } stage += 1;

	if (not (list.size() == 0)) { goto out; }

	return;

out:
	log::err("linked list test failed at stage %d", stage);
	throw "linked lisst test failed";
}

} // namespace tests
} // namespace datastructure
} // namespace openage
