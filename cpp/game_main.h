// Copyright 2014-2014 the openage authors. See copying.md for legal info.

#ifndef OPENAGE_GAME_MAIN_H_
#define OPENAGE_GAME_MAIN_H_

#include <SDL2/SDL.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "args.h"
#include "assetmanager.h"
#include "engine.h"
#include "coord/tile.h"
#include "handlers.h"
#include "terrain/terrain.h"
#include "terrain/terrain_object.h"
#include "gamedata/graphic.gen.h"
#include "util/externalprofiler.h"
#include "gamedata/gamedata.gen.h"
#include "job/job.h"


namespace openage {


/**
 * runs the game.
 */
int run_game(openage::Arguments *args);

void gametest_init(openage::Engine *engine);
void gametest_destroy();

bool on_engine_tick();
bool draw_method();
bool hud_draw_method();
bool input_handler(SDL_Event *e);

class TestBuilding {
public:
	openage::Texture *texture;
	std::string name;
	openage::coord::tile_delta foundation_size;
	int foundation_terrain;
	int sound_id_creation;
	int sound_id_destruction;
};

class TestSound {
public:
	void play();

	std::vector<int> sound_items;
};

class GameMain :
		openage::InputHandler,
		openage::DrawHandler,
		openage::HudHandler,
		openage::TickHandler {
public:
	GameMain(openage::Engine *engine);
	~GameMain();

	void move_camera();

	virtual bool on_tick();
	virtual bool on_draw();
	virtual bool on_drawhud();
	virtual bool on_input(SDL_Event *e);

	/**
	 * all available buildings.
	 */
	std::vector<TestBuilding *> available_buildings;

	/**
	 * all available sounds.
	 */
	std::unordered_map<int, TestSound> available_sounds;

	/**
	 * map graphic id to gamedata graphic.
	 */
	std::unordered_map<int, gamedata::graphic *> graphics;


	/**
	 * all the buildings that have been placed.
	 */
	std::unordered_set<openage::TerrainObject *> placed_buildings;

	/**
	 * debug function that draws a simple overlay grid
	 */
	void draw_debug_grid();

	// currently selected terrain id
	openage::terrain_t editor_current_terrain;
	int editor_current_building;

	bool debug_grid_active;
	bool clicking_active;
	bool ctrl_active;
	bool scrolling_active;

	Terrain *terrain;
	Texture *gaben;

	AssetManager assetmanager;

	util::ExternalProfiler external_profiler;
private:
	void on_gamedata_loaded(std::vector<gamedata::empiresdat> &gamedata);

	bool gamedata_loaded;
	openage::job::Job<std::vector<gamedata::empiresdat>> gamedata_load_job;

	openage::Engine *engine;
};

} //namespace openage

#endif
