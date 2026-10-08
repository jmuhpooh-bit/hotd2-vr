/*
	A panel with a few lines of text in the headset (hotd2-vr), for what the app has to say
	when there's no game to show: making the agent's hands at the first start (hands_rip.h).

	Copyright 2026 mikermak. This file is part of Flycast and is distributed under the GNU GPL v2 or later.
*/
#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace vr::xr
{

struct PanelLine
{
	std::string text;
	float size;			// letter height, of the panel's height
	glm::vec3 colour;
};

struct Panel
{
	std::vector<PanelLine> lines;
	float progress = -1.f;	// a bar under the text, 0..1 (below 0: none)
	std::string footer;		// small, at the bottom
};

// The panel, 1.3 m wide, 1.6 m in front of where the game camera is, at eye height. The
// room is dark around it. viewProj is the eye's (room space, relative to the game camera's
// place). Its picture is made again only when what it says changes.
void drawPanel(const glm::mat4& viewProj, const Panel& panel);
void termPanel();

}
