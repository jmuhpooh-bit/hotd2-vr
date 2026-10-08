/*
	The agent's hands and pistol, made on the spot from the player's own game (hotd2-vr).

	With no hands model yet, the first start of The House of the Dead 2 (PAL) runs the game by
	itself, fast and silent, to the game over scene: Start through the title and Arcade mode,
	then no input, so the agent loses and kneels with his pistol in his hand. That frame is
	taken apart into the model (hands_build.h) and saved; the game goes on as usual. On the
	headset a panel says what's going on, and the gun's B skips it (the arcade gun then, and
	another go at the next start). Whenever the scene comes up in play, it's taken too.

	Copyright 2026 mikermak. This file is part of Flycast and is distributed under the GNU GPL v2 or later.
*/
#pragma once
#include <string>

struct rend_context;
struct RenderPass;

namespace vr::hands
{

// Where the hands model is kept (the user data directory), and where older builds put it.
std::string modelPath();
std::string legacyModelPath();

// Every parsed render pass (vr::dropShotMarker): looks for the game over scene while there's
// no model, and makes it.
void observePass(const rend_context& ctx, const RenderPass& pass, const RenderPass& prev);

// A new model was saved since the last call (the renderer loads it then).
bool takeNewModel();

// The game is being run to the game over scene: no player input, a panel instead of the game.
bool preparing();
// How far along that is, 0..1 (a guess from the time the run usually takes).
float prepProgress();
// The player doesn't want to wait: the arcade gun this time.
void skipPrep();

}
