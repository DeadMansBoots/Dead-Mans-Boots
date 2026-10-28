/*
 * CursorHardware.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

class IImage;
struct SDL_Surface;
struct SDL_Texture;
struct SDL_Cursor;

#include "../../lib/Point.h"
#include "../render/ICursor.h"

class CursorHardware : public ICursor
{
	std::shared_ptr<IImage> cursorImage;

	SDL_Cursor * cursor;

	/// DMB (the project owner, October 22nd 17:5x: "my cursor is flickering nonstop"): each change of the cursor's picture made a new OS cursor and freed the old one, and
	/// his recording shows the cursor gone for a frame at each change as the mouse crosses tiles. A picture's OS cursor is now made once and kept; a change
	/// only switches to it (SDL_SetCursor), the same pictures and hotspots as before. Used on the main thread only.
	struct CachedCursor
	{
		std::shared_ptr<IImage> image; ///< held, so its address cannot come back as another picture's
		Point pivot;
		int scalingPercent;
		int scalingFactor;
		SDL_Cursor * cursor;
	};
	std::vector<CachedCursor> cached; ///< the most recently shown last
	/// pending main-thread work for this cursor checks it before it runs, since the object can go first (a switch to the software cursor)
	std::shared_ptr<bool> alive = std::make_shared<bool>(true);

	SDL_Cursor * create(const std::shared_ptr<IImage> & image, const Point & pivotOffset, int scalingPercent) const;
	void apply(const std::shared_ptr<IImage> & image, const Point & pivotOffset);

public:
	CursorHardware();
	~CursorHardware();

	void setImage(std::shared_ptr<IImage> image, const Point & pivotOffset) override;
	void setCursorPosition( const Point & newPos ) override;
	void render() override;
	void update() override;
	void setVisible( bool on) override;
};

