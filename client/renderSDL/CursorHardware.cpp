/*
 * CursorHardware.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CursorHardware.h"

#include "SDL_Extensions.h"
#include "SDLImageScaler.h"

#include "../GameEngine.h"
#include "../render/IScreenHandler.h"
#include "../render/Colors.h"
#include "../render/IImage.h"

#include "../../lib/CConfigHandler.h"

#include <SDL_render.h>
#include <SDL_events.h>

CursorHardware::CursorHardware():
	cursor(nullptr)
{
	SDL_ShowCursor(SDL_DISABLE);
}

CursorHardware::~CursorHardware()
{
	for(const auto & entry : cached)
		SDL_FreeCursor(entry.cursor);
}

void CursorHardware::setVisible(bool on)
{
	ENGINE->dispatchMainThread([on]()
	{
		if (on)
			SDL_ShowCursor(SDL_ENABLE);
		else
			SDL_ShowCursor(SDL_DISABLE);
	});
}

void CursorHardware::setImage(std::shared_ptr<IImage> image, const Point & pivotOffset)
{
	ENGINE->dispatchMainThread([this, image, pivotOffset, weak = std::weak_ptr<bool>(alive)]()
	{
		if(!weak.expired())
			apply(image, pivotOffset);
	});
}

void CursorHardware::apply(const std::shared_ptr<IImage> & image, const Point & pivotOffset)
{
	const int scalingPercent = static_cast<int>(ENGINE->screenHandler().getInterfaceScalingPercentage() * settings["video"]["cursorScalingFactor"].Float());
	const int scalingFactor = ENGINE->screenHandler().getScalingFactor();
	auto found = std::find_if(cached.begin(), cached.end(), [&](const CachedCursor & entry)
	{
		return entry.image == image && entry.pivot == pivotOffset && entry.scalingPercent == scalingPercent && entry.scalingFactor == scalingFactor;
	});
	if(found == cached.end())
	{
		SDL_Cursor * made = create(image, pivotOffset, scalingPercent);
		if(!made)
			return;
		cached.push_back({image, pivotOffset, scalingPercent, scalingFactor, made});
		logGlobal->debug("Hardware cursor: a new OS cursor, %d kept", static_cast<int>(cached.size()));
	}
	else
		std::rotate(found, std::next(found), cached.end());
	if(cached.back().cursor != cursor)
	{
		cursor = cached.back().cursor;
		SDL_SetCursor(cursor);
	}
	// the pictures not shown for longest go when more are kept than the game has cursors (an artifact being dragged brings its own); the one shown is last
	constexpr size_t keep = 64;
	while(cached.size() > keep)
	{
		SDL_FreeCursor(cached.front().cursor);
		cached.erase(cached.begin());
	}
}

SDL_Cursor * CursorHardware::create(const std::shared_ptr<IImage> & image, const Point & pivotOffset, int cursorScalingPercent) const
{
	Point cursorDimensions = image->dimensions() * ENGINE->screenHandler().getScalingFactor();
	Point cursorDimensionsScaled = image->dimensions() * cursorScalingPercent / 100;
	Point pivotOffsetScaled = pivotOffset * cursorScalingPercent / 100 / ENGINE->screenHandler().getScalingFactor();

	auto cursorSurface = CSDL_Ext::newSurface(cursorDimensions);

	CSDL_Ext::fillSurface(cursorSurface, CSDL_Ext::toSDL(Colors::TRANSPARENCY));

	image->draw(cursorSurface, Point(0,0), nullptr, ENGINE->screenHandler().getScalingFactor());

	SDLImageScaler scaler(cursorSurface);
	scaler.scaleSurface(cursorDimensionsScaled, EScalingAlgorithm::BILINEAR);
	SDL_Surface	* cursorSurfaceScaled = scaler.acquireResultSurface();

	SDL_Cursor * made = SDL_CreateColorCursor(cursorSurfaceScaled, pivotOffsetScaled.x, pivotOffsetScaled.y);

	if (!made)
		logGlobal->error("Failed to set cursor! SDL says %s", SDL_GetError());

	SDL_FreeSurface(cursorSurface);
	SDL_FreeSurface(cursorSurfaceScaled);
	return made;
}

void CursorHardware::setCursorPosition( const Point & newPos )
{
	//no-op
}

void CursorHardware::render()
{
	//no-op
}

void CursorHardware::update()
{
	//no-op
}
