// Copyright 2014-2014 the openage authors. See copying.md for legal info.

#include <cmath>

#include "path.h"
#include "../terrain/terrain.h"

namespace openage {
namespace path {

Node::Node(const coord::phys3 &pos, node_pt prev)
	:
	position(pos),
	tile_position(pos.to_tile3().to_tile()),
	dir_ne(0.0f),
	dir_se(0.0f),
	visited(false),
	was_best(false),
	factor(1.0f),
	path_predecessor(prev) {


	if (prev) {
		cost_t dx = this->position.ne - prev->position.ne;
		cost_t dy = this->position.se - prev->position.se;
		cost_t hyp = std::hypot(dx, dy);
		this->dir_ne = dx / hyp;
		this->dir_se = dy / hyp;
		cost_t similarity = this->dir_ne * prev->dir_ne + this->dir_se * prev->dir_se;
		factor += (1 - similarity);
	}
}


Node::Node(const coord::phys3 &pos, node_pt prev, cost_t past, cost_t heuristic)
	:
	Node(pos, prev) {
	this->past_cost = past;
	this->heuristic_cost = heuristic;
	this->future_cost = past + heuristic;
}

Node::~Node() {

}

bool Node::operator <(const Node &other) const {
	return this->future_cost < other.future_cost;
}

bool Node::operator ==(const Node &other) const {
	return this->position == other.position;
}

cost_t Node::cost_to(const Node &other) const {
	cost_t dx = this->position.ne - other.position.ne;
	cost_t dy = this->position.se - other.position.se;
	return std::hypot(dx, dy) * other.factor * this->factor;
}

Path Node::generate_backtrace() {
	std::vector<Node> waypoints;

	node_pt current = shared_from_this();
	do {
		waypoints.push_back(*current);
		current = current->path_predecessor;
	} while (current != nullptr);
	waypoints.pop_back(); // remove start

	return {waypoints};
}

std::vector<node_pt> Node::get_neighbors(const nodemap_t &nodes, float scale) {
	std::vector<node_pt> neighbors;
	neighbors.reserve(8);
	for (int n = 0; n < 8; ++n) {
		coord::phys3 n_pos = this->position + (neigh_phys[n] * scale);

		if (nodes.count(n_pos) > 0) {
			neighbors.push_back( nodes.at(n_pos) );
		}
		else {
			neighbors.push_back( std::make_shared<Node>(n_pos, shared_from_this()) );
		}
	}
	return neighbors;
}

bool passable_line(node_pt start, node_pt end, std::function<bool(const coord::phys3 &)> passable, float samples) {
	// interpolate between points and make 5 passablity checks (dont check starting pos)
	for (int i = 1; i <= samples; ++i) {
		double percent = (double) i / samples;
		coord::phys_t ne = (1.0 - percent) * start->position.ne + percent * end->position.ne;
		coord::phys_t se = (1.0 - percent) * start->position.se + percent * end->position.se;
		coord::phys_t up = (1.0 - percent) * start->position.up + percent * end->position.up;

		if ( !passable( coord::phys3{ ne, se, up } ) ) {
			return false;
		}
	}
	return true;
}


Path::Path() {

}

Path::Path(const std::vector<Node> &nodes)
	:
	waypoints{nodes} {
}

Path::~Path() {
}

} // namespace path
} // namespace openage
