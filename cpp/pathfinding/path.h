// Copyright 2014-2014 the openage authors. See copying.md for legal info.

#ifndef OPENAGE_PATHFINDING_PATH_H_
#define OPENAGE_PATHFINDING_PATH_H_

#include <list>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

#include "../coord/phys3.h"
#include "../coord/tile.h"
#include "../util/misc.h"
#include "../util/stack_allocator.h"


namespace openage {
namespace path {

class Node;
class Path;

/**
 * the data type for movement cost
 */
using cost_t = float;

/*
 * hash function for tiles
 */
struct tile_hash {
	size_t operator ()(const openage::coord::tile &tile) const {
		size_t nehash = std::hash<openage::coord::tile_t> { }(tile.ne);
		size_t sehash = std::hash<openage::coord::tile_t> { }(tile.se);
		return openage::util::rol<size_t, 1>(nehash) ^ sehash;
	}
};

struct phys3_hash {
	size_t operator ()(const openage::coord::phys3 &pos) const {
		size_t nehash = std::hash<openage::coord::phys_t> { }(pos.ne);
		size_t sehash = std::hash<openage::coord::phys_t> { }(pos.se);
		return openage::util::rol<size_t, 1>(nehash) ^ sehash;
	}
};


using node_pt = Node*;

/*
 * type for mapping tiles to nodes
 */
using nodemap_t = std::unordered_map<coord::phys3, node_pt, phys3_hash>;

constexpr unsigned int neigh_shift = 13;
constexpr coord::phys3_delta const neigh_phys[] = {
	{ 1 * (1 << neigh_shift), -1 * (1 << neigh_shift), 0},
	{ 1 * (1 << neigh_shift),  0 * (1 << neigh_shift), 0},
	{ 1 * (1 << neigh_shift),  1 * (1 << neigh_shift), 0},
	{ 0 * (1 << neigh_shift),  1 * (1 << neigh_shift), 0},
	{-1 * (1 << neigh_shift),  1 * (1 << neigh_shift), 0},
	{-1 * (1 << neigh_shift),  0 * (1 << neigh_shift), 0},
	{-1 * (1 << neigh_shift), -1 * (1 << neigh_shift), 0},
	{ 0 * (1 << neigh_shift), -1 * (1 << neigh_shift), 0}
};

/**
 *
 */
bool passable_line(node_pt start, node_pt end, std::function<bool(const coord::phys3 &)>passable, float samples=5.0f);

/**
 * One waypoint in a path.
 */
class Node {
public:
	Node(const coord::phys3 &pos, node_pt prev);
	Node(const coord::phys3 &pos, node_pt prev, cost_t past, cost_t heuristic);
	~Node();

	/**
	 * Orders nodes according to their future cost value.
	 */
	bool operator <(const Node &other) const;

	/**
	 * Compare the node to another one.
	 * They are the same if their position is.
	 */
	bool operator ==(const Node &other) const;

	/**
	 * Calculates the actual movement cose to another node.
	 */
	cost_t cost_to(const Node &other) const;

	/**
	 * Create a backtrace path beginning at this node.
	 */
	Path generate_backtrace();

	/**
	 * Get all neighbors of this graph node.
	 */
	std::vector<node_pt> get_neighbors(const nodemap_t &,
                                       util::stack_allocator<Node>& alloc,
                                       float scale=1.0f);

	/**
	 * The tile position this node is associated to.
	 * todo make const
	 */
	coord::phys3 position;
	coord::tile tile_position;
	cost_t dir_ne, dir_se; // for path smoothing

	/**
	 * Future cost estimation value for this node.
	 */
	cost_t future_cost;

	/**
	 * Evaluated past cost value for the node.
	 * This stores the actual cost from start to this node.
	 */
	cost_t past_cost;

	/**
	 * Heuristic cost cache.
	 * Calculated once, is the heuristic distance from this node
	 * to the goal.
	 */
	cost_t heuristic_cost;

	/**
	 * Can this node be passed?
	 */
	bool accessible;

	/**
	 * Has this Node been visited?
	 */
	bool visited;

	/**
	 * Does this node already have an alternative path?
	 * If the node was once selected as the best next hop,
	 * this is set to true.
	 */
	bool was_best;

	/**
	 * Factor to adjust movement cost.
	 * default: 1
	 */
	cost_t factor;

	/**
	 * Node where this one was reached by least cost.
	 */
	node_pt path_predecessor;
};


/**
 * Represents a planned trajectory.
 * Generated by pathfinding algorithms.
 */
class Path {
public:
	Path();
	Path(const std::vector<Node> &nodes);
	~Path();

	/**
	 * These are the waypoints to navigate in order.
	 * Includes the start and end node.
	 */
	std::vector<Node> waypoints;
};

} // namespace path
} // namespace openage


namespace std {

/**
 * Hash function for path nodes.
 * Just uses their position.
 */
template <>
struct hash<openage::path::Node &> {
	size_t operator ()(const openage::path::Node &x) const {
		openage::coord::phys3 node_pos = x.position;
		size_t nehash = std::hash<openage::coord::phys_t>{}(node_pos.ne);
		size_t sehash = std::hash<openage::coord::phys_t>{}(node_pos.se);
		return openage::util::rol<size_t, 1>(nehash) ^ sehash;
	}
};

} // namespace std



#endif
