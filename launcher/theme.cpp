/*
 * theme.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "theme.h"
#include "gamefont.h"

#include "../lib/CConfigHandler.h"
#include "../lib/VCMIDirs.h"
#include "../lib/filesystem/Filesystem.h"
#include "../lib/texts/CGeneralTextHandler.h"
#include "../lib/vcmi_endian.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QTransform>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QStyleFactory>
#include <QTimer>
#include <QToolButton>

#ifdef VCMI_WINDOWS
#include <windows.h>
#endif

namespace
{
/// whether windows get Windows' dark title bar: in the leather and dark looks
bool darkFrames = false;

/// whether the carved words on the buttons are light, for a primary slot of black, where the game's black letters would vanish
bool lightLettering = false;

/// the height every launcher button's gold bar is drawn at (wordlessBar), the game's own 40 px bars
/// scaled down to the launcher's own, smaller buttons; a carved word is scaled the same amount, so its
/// letters stay in the bar's own proportion instead of standing oversized on a shrunk bar
constexpr int BAR_HEIGHT = 28;

/// a label on two lines (too long for its button on one): the game's own two line buttons set their lines 1.5 capital heights apart, and the bar
/// is drawn tall enough for both lines (the picture of two lines is 42 px, the bar's four pixel borders and one pixel of padding on each side make 52)
constexpr double LINE_PITCH = 1.5;
constexpr int STACK_BAR_HEIGHT = 52;

/// A window's title bar in Windows' dark frame (Windows 10 1809 and later), or back in its light one;
/// elsewhere the title bar is the window manager's
void frameWindow(QWidget * window)
{
#ifdef VCMI_WINDOWS
	using SetAttribute = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
	static const auto setAttribute = reinterpret_cast<SetAttribute>(
		reinterpret_cast<void *>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmSetWindowAttribute")));
	if(!setAttribute || !window || !window->isWindow())
		return;
	const BOOL dark = darkFrames ? TRUE : FALSE;
	const auto handle = reinterpret_cast<HWND>(window->winId());
	setAttribute(handle, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE
	// the frame repaints only when told it changed
	SetWindowPos(handle, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
	(void)window;
#endif
}

/// DMB: carves every QPushButton under `window` into the game's gold-button lettering, or plain if the
/// look is not leather or dark; defined with the rest of that work, further down this file
void carveEveryButtonUnder(QWidget * window, bool active);
void applyCarvedButtonWord(QPushButton * button, bool active);
/// whether the word carved on `button` is wider than the room the button has for it
bool carvedWordTooWide(const QPushButton * button);

/// frames every window as it first shows, dialogs included, and carves every button's word into the
/// game's gold-button lettering (K's testing found the first cut of this missed the Settings page's
/// own category buttons, September 28th: they sit on a tab that is not the one shown first, so the
/// main window's own Show, the only signal read before this, came and went before they existed on a
/// visible page at all). A button not yet on a shown page still fires its own Show once it is (Qt
/// shows every descendant of a newly-current QStackedWidget page); read one event loop turn late
/// (QTimer::singleShot(0, ...)), since its layout, and so its width, is not real any earlier than that
/// either (found testing the very first buttons this same morning).
class FrameEveryWindow : public QObject
{
public:
	bool eventFilter(QObject * watched, QEvent * event) override
	{
		// a button's own width is only real once its layout has run, and moves with the window and the column: a word that did not fit
		// the width it was first carved at is tried again at a new one, and a carved word wider than its button's room is set smaller.
		// A word is never set larger again: a bigger picture makes the button's size hint bigger, which makes the column wider, which
		// would make the word bigger
		// a plain setText on a carved button leaves the old picture beside the new text: the next paint carves the new word
		if(event->type() == QEvent::Paint)
		{
			if(auto * button = qobject_cast<QPushButton *>(watched); button && !button->text().isEmpty() && button->property("dmbCarvedWordWidth").toInt() > 0)
			{
				QPointer<QPushButton> guarded(button);
				const bool active = darkFrames;
				QTimer::singleShot(0, [guarded, active]()
				{
					if(guarded && !guarded->text().isEmpty() && guarded->property("dmbCarvedWordWidth").toInt() > 0)
						applyCarvedButtonWord(guarded, active);
				});
			}
			return false;
		}
		if(event->type() == QEvent::Resize)
		{
			if(auto * button = qobject_cast<QPushButton *>(watched); button && button->property("dmbCarvedFrom").isValid())
			{
				QPointer<QPushButton> guarded(button);
				const bool active = darkFrames;
				QTimer::singleShot(0, [guarded, active]()
				{
					if(!guarded)
						return;
					const bool notCarved = guarded->property("dmbCarvedWordWidth").toInt() == 0;
					const bool retry = notCarved && guarded->property("dmbCarvedWidth").toInt() != guarded->width();
					if(retry || carvedWordTooWide(guarded))
						applyCarvedButtonWord(guarded, active);
				});
			}
			return false;
		}
		if(event->type() != QEvent::Show)
			return false;
		if(auto * widget = qobject_cast<QWidget *>(watched); widget && widget->isWindow())
			frameWindow(widget);
		if(auto * button = qobject_cast<QPushButton *>(watched))
		{
			QPointer<QPushButton> guarded(button);
			const bool active = darkFrames;
			QTimer::singleShot(0, [guarded, active]()
			{
				if(guarded)
					applyCarvedButtonWord(guarded, active);
			});
		}
		return false;
	}
};

/// A Heroes III PCX (8-bit with its palette at the end, or 24-bit), read as the map editor reads them
QImage readH3Pcx(const ui8 * pcx, size_t size)
{
	if(size < 12)
		return {};
	// DMB: the sizes are compared in 64 bits, where 32-bit products wrapped and let a 65536 x 65536 header over a 780-byte file
	// through, and a side past INT_MAX is refused before QImage takes it as an int
	const uint64_t bytes = read_le_u32(pcx);
	const uint64_t width = read_le_u32(pcx + 4);
	const uint64_t height = read_le_u32(pcx + 8);
	if(width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max())
		return {};
	if(bytes == width * height && size >= 12 + bytes + 256 * 3)
	{
		QImage image(pcx + 12, width, height, width, QImage::Format_Indexed8);
		QVector<QRgb> colours;
		const ui8 * palette = pcx + size - 256 * 3;
		for(int i = 0; i < 256; ++i)
			colours.append(qRgb(palette[3 * i], palette[3 * i + 1], palette[3 * i + 2]));
		image.setColorTable(colours);
		return image.copy(); // the caller's buffer goes away
	}
	if(bytes == width * height * 3 && size >= 12 + bytes)
		return QImage(pcx + 12, width, height, width * 3, QImage::Format_BGR888).copy();
	return {};
}

/// DMB: a listed resource's bytes, or nothing, with the reason in the log, when it cannot be read (a damaged entry in an imported
/// archive). The look then shows that part as unimported, where the exception ended the launcher on every start and kept the
/// player from Settings, where the look is changed
std::optional<std::pair<std::unique_ptr<ui8[]>, si64>> readResource(const ResourcePath & path)
{
	try
	{
		return CResourceHandler::get()->load(path)->readAll();
	}
	catch(const std::exception & e)
	{
		logGlobal->error("Launcher look: %s is listed but could not be read (%s); the look shows without it", path.getOriginalName(), e.what());
		return std::nullopt;
	}
}

/// The leather Heroes III fills its windows with, from the player's own game files; null before an import
QPixmap playerLeather()
{
	const ResourcePath path("DATA/DIBOXBCK", EResType::IMAGE);
	if(!CResourceHandler::get()->existsResource(path))
		return {};
	const auto read = readResource(path);
	if(!read)
		return {};
	const auto & data = *read;
	const auto * bytes = reinterpret_cast<const ui8 *>(data.first.get());
	QImage image = readH3Pcx(bytes, data.second);
	if(image.isNull()) // a mod may put a PNG in its place
		image.loadFromData(bytes, static_cast<int>(data.second));
	return QPixmap::fromImage(image);
}

/// the largest side a DEF frame's box may have: Heroes III draws none larger than its 800 x 600 screen, and this leaves room for a mod's
constexpr int MAX_DEF_FRAME_SIDE = 4096;

/// A frame of a Heroes III sprite (DEF), decoded as the game's own loader decodes one (client
/// CDefFile): palette index 0 transparent, 1 to 7 the game's shadow, the rest the sprite's palette
QImage readH3DefFrame(const ui8 * def, size_t size, size_t wanted)
{
	const auto fits = [size](size_t at, size_t length) { return at + length <= size; };
	if(!fits(0, 16 + 768))
		return {};
	const ui32 blocks = read_le_u32(def + 12);
	QVector<QRgb> colours;
	for(int i = 0; i < 256; ++i)
		colours.append(i == 0 ? qRgba(0, 0, 0, 0) : i < 8 ? qRgba(0, 0, 0, 128) : qRgb(def[16 + 3 * i], def[17 + 3 * i], def[18 + 3 * i]));

	std::vector<ui32> offsets;
	size_t it = 16 + 768;
	for(ui32 block = 0; block < blocks && fits(it, 16); ++block)
	{
		const ui32 entries = read_le_u32(def + it + 4);
		it += 16 + 13 * static_cast<size_t>(entries);
		for(ui32 entry = 0; entry < entries && fits(it, 4); ++entry, it += 4)
			offsets.push_back(read_le_u32(def + it));
	}
	if(wanted >= offsets.size() || !fits(offsets[wanted], 32))
		return {};

	const ui8 * frame = def + offsets[wanted];
	const size_t base = offsets[wanted] + 32;
	const ui32 format = read_le_u32(frame + 4);
	const int fullWidth = static_cast<int>(read_le_u32(frame + 8));
	const int fullHeight = static_cast<int>(read_le_u32(frame + 12));
	const int width = static_cast<int>(read_le_u32(frame + 16));
	const int height = static_cast<int>(read_le_u32(frame + 20));
	const int left = static_cast<int>(read_le_u32(frame + 24));
	const int top = static_cast<int>(read_le_u32(frame + 28));
	// DMB: the frame's place is checked without adding two of its fields (left + width overflowed int and passed), the box is held
	// to MAX_DEF_FRAME_SIDE, and an image Qt could not allocate is refused, where scanLine() of a null image was written through
	if(fullWidth <= 0 || fullHeight <= 0 || fullWidth > MAX_DEF_FRAME_SIDE || fullHeight > MAX_DEF_FRAME_SIDE || width < 0 || height < 0
		|| left < 0 || top < 0 || width > fullWidth - left || height > fullHeight - top)
		return {};
	if(format > 3 || (format == 2 && !fits(base, 2)))
		return {};

	QImage image(fullWidth, fullHeight, QImage::Format_Indexed8);
	if(image.isNull())
		return {};
	image.setColorTable(colours);
	image.fill(0);
	size_t following = format == 2 ? base + read_le_u16(def + base) : 0; // format 2's lines follow one another
	for(int y = 0; y < height; ++y)
	{
		uchar * line = image.scanLine(top + y) + left;
		size_t at = following;
		if(format == 0)
		{
			at = base + static_cast<size_t>(y) * width;
			if(!fits(at, width))
				return {};
			std::copy(def + at, def + at + width, line);
			continue;
		}
		if(format == 1)
		{
			if(!fits(base + 4 * static_cast<size_t>(y), 4))
				return {};
			at = base + read_le_u32(def + base + 4 * y);
		}
		else if(format == 3)
		{
			const size_t entry = base + 2 * static_cast<size_t>(y) * (width / 32);
			if(!fits(entry, 2))
				return {};
			at = base + read_le_u16(def + entry);
		}
		// one line of segments: format 1 a code byte then a length, 0xFF for raw pixels; formats 2 and 3
		// one byte, the code in its top three bits (7 for raw) and the length below
		int done = 0;
		while(done < width)
		{
			if(!fits(at, format == 1 ? 2 : 1))
				return {};
			int code, length;
			bool raw;
			if(format == 1)
			{
				code = def[at];
				length = def[at + 1] + 1;
				raw = code == 0xFF;
				at += 2;
			}
			else
			{
				code = def[at] >> 5;
				length = (def[at] & 31) + 1;
				raw = code == 7;
				at += 1;
			}
			length = std::min(length, width - done);
			if(raw)
			{
				if(!fits(at, length))
					return {};
				std::copy(def + at, def + at + length, line + done);
				at += length;
			}
			else
				std::fill(line + done, line + done + length, static_cast<uchar>(code));
			done += length;
		}
		following = at;
	}
	return image.convertToFormat(QImage::Format_ARGB32);
}

/// Fills a rectangular hole with a bilinear Coons patch built from its own four edges (each a 7-wide
/// smoothed running average), the same construction as AssetGenerator's letterSizeButton, ported from
/// Canvas/ColorRGBA to QImage/QRgb: the surrounding art's own colours interpolate across the hole
/// instead of a flat fill, reading as continuous where the true content behind the hole was smooth (no
/// hard internal edges). Used to remove text baked into a real game asset's own pixels, DMB's standing
/// rule for this class of problem (K, September 28th; this file's own CLAUDE.md).
void coonsPatchInpaint(QImage & image, int l, int t, int r, int b)
{
	struct Colour { double r, g, b; };
	const auto smooth = [](const std::vector<QRgb> & colours)
	{
		std::vector<Colour> out;
		for(size_t i = 0; i < colours.size(); ++i)
		{
			double sum[3] = {0, 0, 0};
			int count = 0;
			for(size_t j = i >= 3 ? i - 3 : 0; j <= std::min(colours.size() - 1, i + 3); ++j, ++count)
			{
				sum[0] += qRed(colours[j]);
				sum[1] += qGreen(colours[j]);
				sum[2] += qBlue(colours[j]);
			}
			out.push_back({sum[0] / count, sum[1] / count, sum[2] / count});
		}
		return out;
	};
	std::vector<QRgb> leftPts, rightPts, topPts, bottomPts;
	for(int y = t; y < b; ++y)
	{
		leftPts.push_back(image.pixel(l, y));
		rightPts.push_back(image.pixel(r - 1, y));
	}
	for(int x = l; x < r; ++x)
	{
		topPts.push_back(image.pixel(x, t));
		bottomPts.push_back(image.pixel(x, b - 1));
	}
	const auto leftEdge = smooth(leftPts), rightEdge = smooth(rightPts), topEdge = smooth(topPts), bottomEdge = smooth(bottomPts);
	const double w = r - l - 1, h = b - t - 1;
	for(int y = t + 1; y < b - 1; ++y)
	{
		const double v = (y - t) / h;
		for(int x = l + 1; x < r - 1; ++x)
		{
			const double u = (x - l) / w;
			const auto channel = [&](double Colour::* member)
			{
				const double sides = (1 - u) * (leftEdge[y - t].*member) + u * (rightEdge[y - t].*member);
				const double ends = (1 - v) * (topEdge[x - l].*member) + v * (bottomEdge[x - l].*member);
				const double corners = (1 - u) * (1 - v) * (topEdge.front().*member) + u * (1 - v) * (topEdge.back().*member)
					+ (1 - u) * v * (bottomEdge.front().*member) + u * v * (bottomEdge.back().*member);
				return std::clamp(static_cast<int>(std::lround(sides + ends - corners)), 0, 255);
			};
			image.setPixel(x, y, qRgba(channel(&Colour::r), channel(&Colour::g), channel(&Colour::b), qAlpha(image.pixel(x, y))));
		}
	}
}

/// Heroes III's gold bar, the Random Map Setup's "Show random maps" button (RANSHOW) with its words
/// taken out as DMB's RanShowButton pieces are (AssetGenerator): the 21-pixel scrollwork ends, and one
/// column of the brushed middle (110) repeated, at the launcher's button height
QImage wordlessBar(const QImage & frame, int height = BAR_HEIGHT)
{
	if(frame.width() < 2 * 21 + 112)
		return {};
	QImage bar(2 * 21 + 64, frame.height(), QImage::Format_ARGB32);
	bar.fill(Qt::transparent);
	QPainter painter(&bar);
	painter.drawImage(0, 0, frame, 0, 0, 21, frame.height());
	for(int x = 21; x < bar.width() - 21; ++x)
		painter.drawImage(x, 0, frame, 110, 0, 1, frame.height());
	painter.drawImage(bar.width() - 21, 0, frame, frame.width() - 21, 0, 21, frame.height());
	painter.end();
	return bar.scaledToHeight(height, Qt::SmoothTransformation);
}

/// `base` (the one line bar) drawn `height` high with its scrollwork ends and brushed middle at the SAME size: the four pixel borders top and bottom kept, and the rows
/// between them mirrored over and over, centred on the bar's middle, so the carved ornaments of a tall button are the ornaments of a short one and sit at the
/// same place from the button's middle (K, October 16th: "this side garnish ... is visually out of sync with the rest just because the button is smaller
/// vertically"; the tall bar used to be the game's frame scaled up to 52 px, its ornaments nearly twice the size of the one line bar's)
QImage tallBar(const QImage & base, int height)
{
	constexpr int border = 4;
	const int inner = base.height() - 2 * border;
	if(base.isNull() || inner < 2 || height <= base.height())
		return base;
	QImage out(base.width(), height, QImage::Format_ARGB32);
	out.fill(Qt::transparent);
	QPainter painter(&out);
	painter.drawImage(0, 0, base, 0, 0, base.width(), border);
	painter.drawImage(0, height - border, base, 0, base.height() - border, base.width(), border);
	const int rows = height - 2 * border;
	const int shift = ((inner / 2 - rows / 2) % (2 * inner) + 2 * inner) % (2 * inner);
	for(int i = 0; i < rows; ++i)
	{
		int row = (i + shift) % (2 * inner);
		if(row >= inner)
			row = 2 * inner - 1 - row;
		painter.drawImage(0, border + i, base, 0, border + row, base.width(), 1);
	}
	painter.end();
	return out;
}

/// Heroes III's blue marbled choice button, the Random Map Setup's (RANWEAK), without its words as
/// DMB's RanButton pieces are: the 6-pixel ends and its two word-free middles, 6-14 and 70-76
QImage wordlessChoice(const QImage & frame)
{
	if(frame.width() < 83)
		return {};
	QImage piece(6 + 9 + 7 + 6, frame.height(), QImage::Format_ARGB32);
	piece.fill(Qt::transparent);
	QPainter painter(&piece);
	painter.drawImage(0, 0, frame, 0, 0, 6, frame.height());
	painter.drawImage(6, 0, frame, 6, 0, 9, frame.height());
	painter.drawImage(15, 0, frame, 70, 0, 7, frame.height());
	painter.drawImage(22, 0, frame, frame.width() - 6, 0, 6, frame.height());
	painter.end();
	return piece;
}

/// The colours a launcher slot (background, partitions, primary buttons, secondary buttons) can take, K, October 1st:
/// "black is definitely allowed in the background slot, andi suppose silver can be too, why not ... everything is
/// configurable now, so by default red with dark mode, but each should be changeable. One color per slot. max
/// configurability." The eight hero colours are the game's own player colours: the six numbers each has in the
/// game's config (settings, interface, playerColoredBackground) are the range each colour channel of the game's
/// brown leather is stretched to for that player, and are used here the same way on the lightness of any art.
const std::vector<std::string> SLOT_COLOUR_NAMES = {"red", "blue", "tan", "green", "orange", "purple", "teal", "pink", "silver", "black"};

using ColourRange = std::array<float, 6>; // min r, g, b, max r, g, b

/// The game's player colours: from its own config when the launcher can read it, else the numbers it ships with
const std::map<std::string, ColourRange> & heroRanges()
{
	static const std::map<std::string, ColourRange> ranges = []()
	{
		std::map<std::string, ColourRange> shipped = {
			{"red",    {0.25f, 0.00f, 0.00f, 1.25f, 0.00f, 0.00f}},
			{"blue",   {0.00f, 0.00f, 0.00f, 0.45f, 1.20f, 4.50f}},
			{"tan",    {0.40f, 0.27f, 0.23f, 1.10f, 1.20f, 1.15f}},
			{"green",  {-0.27f, 0.10f, -0.27f, 0.70f, 1.70f, 0.70f}},
			{"orange", {0.47f, 0.17f, -0.27f, 1.60f, 1.20f, 0.70f}},
			{"purple", {0.12f, -0.10f, 0.25f, 1.15f, 1.20f, 2.20f}},
			{"teal",   {-0.13f, 0.23f, 0.23f, 0.90f, 1.20f, 2.20f}},
			{"pink",   {0.44f, 0.15f, 0.25f, 1.00f, 1.00f, 1.75f}},
		};
		try
		{
			const JsonNode config(JsonPath::builtin("config/gameConfig.json"));
			const JsonNode & filters = config["settings"]["interface"]["playerColoredBackground"];
			for(auto & [name, range] : shipped)
			{
				const auto & numbers = filters[name].Vector();
				if(numbers.size() == range.size())
					for(size_t i = 0; i < range.size(); ++i)
						range[i] = static_cast<float>(numbers[i].Float());
			}
		}
		catch(const std::exception &)
		{
			// the numbers above stand
		}
		return shipped;
	}();
	return ranges;
}

/// One pixel of art as `colour`, keeping its lightness (so the bevels, the marbling and the grain stay): a hero
/// colour through the game's range for that player, softened a tenth of the way back to grey; silver and black
/// as greys; `weight` darkens (the dark look). `lightness` is 0 to 255.
QRgb tintPixel(int lightness, const std::string & colour, double weight)
{
	const double l = std::clamp(lightness, 0, 255) / 255.0;
	double r, g, b;
	if(colour == "silver")
	{
		// the cool tint is added after the weight, so a lifted silver does not turn blue
		const auto grey = [&](double tint) { return std::clamp(static_cast<int>(std::lround((l * 0.86 * weight + tint / 255.0) * 255.0)), 0, 255); };
		return qRgb(grey(0), grey(3), grey(8));
	}
	else if(colour == "black")
	{
		const auto grey = [&](double tint) { return std::clamp(static_cast<int>(std::lround((l * 0.30 * weight + tint / 255.0) * 255.0)), 0, 255); };
		return qRgb(grey(0), grey(0), grey(2));
	}
	else
	{
		const auto found = heroRanges().find(colour);
		if(found == heroRanges().end())
			return qRgb(lightness, lightness, lightness);
		const ColourRange & range = found->second;
		// the lightest art (a button's highlight) a little below the colour's own top, or a pale colour such as tan
		// would turn it white; softened only a tenth of the way back to grey, so the colour reads
		const double lit = l * 0.85;
		const auto channel = [&](int c) { return (lit * (range[3 + c] - range[c]) + range[c]) * 0.9 + lit * 0.1; };
		r = channel(0);
		g = channel(1);
		b = channel(2);
	}
	const auto byte = [weight](double v) { return std::clamp(static_cast<int>(std::lround(v * weight * 255.0)), 0, 255); };
	return qRgb(byte(r), byte(g), byte(b));
}

/// `image` as `colour` (tintPixel on each pixel's lightness; the alpha stays)
QImage tinted(const QImage & image, const std::string & colour, double weight)
{
	QImage out = image.convertToFormat(QImage::Format_ARGB32);
	for(int y = 0; y < out.height(); ++y)
	{
		auto * line = reinterpret_cast<QRgb *>(out.scanLine(y));
		for(int x = 0; x < out.width(); ++x)
			line[x] = (tintPixel(qGray(line[x]), colour, weight) & 0x00FFFFFF) | (static_cast<QRgb>(qAlpha(line[x])) << 24);
	}
	return out;
}

constexpr double SECONDARY_DARK_WEIGHT = 0.8; // the dark look's switches: the game's colour at this much of its brightness


/// DMB's own button face (launcher/fonts/DMBButtonFace.ttf, an open-licence heavy serif cut and renamed by DMB/release/make_button_face.py,
/// licence text beside it): K, October 12th 2026, on the game's letters taken from the player's files and reduced to the launcher's size:
/// "WHAT PART OF BUILD OUR OWN FONT SET DID YOU NOT UNDERSTAND". Every letter, digit and mark of a button's word is drawn from one face at
/// the size the button needs, with the carved look (a dark letter, a light edge on its lower left) put on after, so no stroke is a game
/// letter shrunk and patched. Empty when the file did not load: the button keeps its plain text.
QString buttonFaceFamily()
{
	static const QString family = []()
	{
		const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMBButtonFace.ttf"));
		const QStringList families = id < 0 ? QStringList() : QFontDatabase::applicationFontFamilies(id);
		return families.isEmpty() ? QString() : families.front();
	}();
	return family;
}

/// the height of a button word's capitals, as the game draws them on its bars: 22 rows on a 40 row bar (RANSHOW's own, the bar
/// the launcher's buttons are cut from), so 22/40 of BAR_HEIGHT
constexpr double CAP_OF_BAR = 22.0 / 40.0;

constexpr double BUTTON_FACE_WIDTH = 1.0; // the face already has the game's widths, glyph by glyph (make_button_face.py)

/// a word's outline in 100 pixel type (the face's own spacing, kerning on); its left edge at x = 0, its baseline at y = 0. Every size is this
/// outline scaled, so a size is never rounded to a whole pixel of type and a shrunk word keeps its proportions exactly
constexpr double BUTTON_FACE_PROBE = 100.0;
QPainterPath buttonWordOutline(const QString & word)
{
	QFont font(buttonFaceFamily());
	font.setPixelSize(static_cast<int>(BUTTON_FACE_PROBE));
	font.setHintingPreference(QFont::PreferNoHinting);
	font.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::ForceOutline));
	font.setKerning(true);
	QPainterPath path;
	path.addText(QPointF(0, 0), font, word);
	return path;
}

/// the outline at `pixelSize` pixels of type. The game's letters are broader than this face's: the whole face is drawn BUTTON_FACE_WIDTH
/// wider, every letter alike (measured against the game's own button words, .tmp/dmb/font/score_faces.py)
QPainterPath buttonWordPath(const QPainterPath & outline, double pixelSize)
{
	const double scale = pixelSize / BUTTON_FACE_PROBE;
	return QTransform().scale(scale * BUTTON_FACE_WIDTH, scale).map(outline);
}

/// `text` as the player reads it: Qt's "&" before a key (the button's own accelerator) is dropped and "&&" is one ampersand; the words one space apart
QString carvedPlain(const QString & text)
{
	QString plain;
	for(int i = 0; i < text.size(); ++i)
	{
		if(text[i] != QLatin1Char('&'))
			plain += text[i];
		else if(i + 1 < text.size() && text[i + 1] == QLatin1Char('&'))
		{
			plain += QLatin1Char('&');
			++i;
		}
	}
	return plain.simplified();
}

/// the lines of a carved label (separated by a new line), each as the player reads it
QStringList carvedLines(const QString & text)
{
	QStringList lines;
	for(const QString & line : text.split(QLatin1Char('\n')))
		if(const QString plain = carvedPlain(line); !plain.isEmpty())
			lines << plain;
	return lines;
}

/// the width of one line at the capital height of a button's word, in logical pixels; 0 when the face lacks a letter of it
double carvedLineWidth(const QString & line)
{
	if(buttonFaceFamily().isEmpty())
		return 0;
	const QString word = line.toUpper();
	if(word.isEmpty())
		return 0;
	const QRawFont raw = QRawFont::fromFont(QFont(buttonFaceFamily()));
	for(const QChar c : word)
		if(!raw.supportsCharacter(c.unicode()))
			return 0;
	const double capOfProbe = -buttonWordOutline(QStringLiteral("H")).boundingRect().top();
	return buttonWordOutline(word).boundingRect().width() * CAP_OF_BAR * BAR_HEIGHT / capOfProbe;
}

/// the width of a label (its widest line) in logical pixels; 0 when the face lacks a letter of it
double carvedWordWidth(const QString & text)
{
	const QStringList lines = carvedLines(text);
	double widest = 0;
	for(const QString & line : lines)
	{
		const double width = carvedLineWidth(line);
		if(width <= 0)
			return 0;
		widest = std::max(widest, width);
	}
	return widest;
}

/// `original` as it is set on a button with `room` logical pixels for its word: on one line where that fits, else on two, split between
/// words where the longer line is shortest (the game's own two line buttons); a word that cannot be split stays on one line
QString carvedStacked(const QString & original, double room)
{
	const QString plain = carvedPlain(original);
	const QStringList words = plain.split(QLatin1Char(' '));
	if(words.size() < 2 || carvedWordWidth(plain) + 4.0 <= room)
		return plain;
	QString best;
	double bestWidth = 0;
	for(int cut = 1; cut < words.size(); ++cut)
	{
		const QString top = words.mid(0, cut).join(QLatin1Char(' '));
		const QString bottom = words.mid(cut).join(QLatin1Char(' '));
		const double widest = std::max(carvedLineWidth(top.toUpper()), carvedLineWidth(bottom.toUpper()));
		if(carvedLineWidth(top.toUpper()) <= 0 || carvedLineWidth(bottom.toUpper()) <= 0)
			continue;
		if(best.isEmpty() || widest < bestWidth)
		{
			best = top + QLatin1Char('\n') + bottom;
			bestWidth = widest;
		}
	}
	return best.isEmpty() ? plain : best;
}

/// `text` (one line, or lines separated by a new line) in the button face, carved: a transparent image at `ratio` device pixels to a logical one, with the
/// capitals always CAP_OF_BAR * BAR_HEIGHT logical pixels high, the lines LINE_PITCH capital heights apart and centred on each other, the word condensed to
/// `scale` of its natural width only where its button is too narrow even so (never made shorter). Null when the face is missing or `text` has a letter the face
/// lacks. `light`: a light letter with a dark edge (the leather look's black slots); `enabled`: a disabled button's word is a grey letter with a faint edge,
/// drawn here and not by Qt's own greying of a picture.
QImage carvedButtonWord(const QString & text, double ratio, double scale = 1.0, bool light = false, bool enabled = true)
{
	if(carvedWordWidth(text) <= 0)
		return {};
	const QStringList lines = carvedLines(text);
	const double wantedCap = CAP_OF_BAR * BAR_HEIGHT * ratio;
	const double capOfProbe = -buttonWordOutline(QStringLiteral("H")).boundingRect().top();
	const double size = BUTTON_FACE_PROBE * wantedCap / capOfProbe;
	const double cap = capOfProbe * size / BUTTON_FACE_PROBE;
	const double edge = std::max(1.0, 0.06 * cap); // the carved edge: a pixel or so, at least a whole device pixel
	const double pitch = LINE_PITCH * cap;
	const int count = static_cast<int>(lines.size());

	std::vector<QPainterPath> paths;
	double widest = 0;
	for(const QString & line : lines)
	{
		paths.push_back(QTransform().scale(scale, 1.0).map(buttonWordPath(buttonWordOutline(line.toUpper()), size)));
		widest = std::max(widest, paths.back().boundingRect().width());
	}
	// the same height for every word (its capitals centred on the middle row), and more only where an accent or a tail needs it, so the buttons' words
	// stand at one level; the lines of a stacked word share the middle row between them
	double reach = cap / 2 + 1 + (count - 1) * pitch / 2;
	for(int i = 0; i < count; ++i)
	{
		const double baseline = (i - (count - 1) / 2.0) * pitch + cap / 2; // from the middle row
		const QRectF box = paths[i].boundingRect();
		reach = std::max({reach, -(baseline + box.top()) + 1, baseline + box.bottom() + 1});
	}
	const double half = std::ceil(reach);
	const int width = static_cast<int>(std::ceil(widest + edge + 2));
	const int height = static_cast<int>(2 * half);
	QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(Qt::NoPen);
		QColor body = light ? QColor(255, 255, 255) : QColor(0, 0, 0);
		QColor rim = light ? QColor(0, 0, 0) : QColor(255, 255, 255);
		if(!enabled)
		{
			body = light ? QColor(150, 150, 150) : QColor(52, 49, 46);
			rim.setAlpha(45);
		}
		for(int i = 0; i < count; ++i)
		{
			const QRectF box = paths[i].boundingRect();
			// each line centred on the widest, its capitals on its own row of the middle, the widest a whole pixel in
			const QPointF origin(1.0 + edge + (widest - box.width()) / 2 - box.left(), half + (i - (count - 1) / 2.0) * pitch + cap / 2);
			painter.setBrush(rim);
			painter.drawPath(paths[i].translated(origin + QPointF(-edge, edge)));
			painter.setBrush(body);
			painter.drawPath(paths[i].translated(origin));
		}
	}
	image.setDevicePixelRatio(ratio);
	return image;
}

/// the logical size of `image` (a carved word at a device pixel ratio)
QSize logicalSize(const QImage & image)
{
	const double ratio = image.devicePixelRatio();
	return QSize(static_cast<int>(std::ceil(image.width() / ratio)), static_cast<int>(std::ceil(image.height() / ratio)));
}

// the bar's own scrollwork ends, kept clear on each side (gameButtonStyle's own 21 px at RANSHOW's
// native size, scaled the same amount the bar is), and a little headroom the button's own frame needs top and bottom
int carvedBarEnds()
{
	return std::max(1, static_cast<int>(std::lround(21 * BAR_HEIGHT / 40.0)));
}
constexpr int carvedClear = 2;

bool carvedWordTooWide(const QPushButton * button)
{
	return button->property("dmbCarvedWordWidth").toInt() > button->width() - 2 * (carvedBarEnds() + carvedClear);
}

/// how far a word may be condensed where it can neither stack nor fit: a fixed width button only; every button the layout sizes is asked for
/// the room its label needs (minimumCarvedButtonWidth)
constexpr double CARVED_CONDENSE_FLOOR = 0.9;

/// `button` in the carved button face where `active` (the look is leather or dark, not "system") and the face has every letter of its label; the label on
/// one line where it fits, else stacked on two (the button then grows as tall as two lines ask, the bar drawn for it by the style sheet's dmbLines
/// property); back to its own plain text where the look is "system" now or the face lacks a letter. Idempotent: safe to call again for a button
/// already carved, or already plain.
void applyCarvedButtonWord(QPushButton * button, bool active)
{
	const QVariant stored = button->property("dmbCarvedFrom");
	QString original = stored.isValid() ? stored.toString() : button->text();
	// DMB: a button with no word of its own (icon only) is not one to carve, and keeps its icon; it is still marked, so a word it is given later is carved. One carved before
	// whose word is now empty goes on below, where its carved picture is taken off and its own icon put back
	if(original.isEmpty() && button->text().isEmpty() && !button->property("dmbCarvedIcon").toBool())
	{
		if(!stored.isValid())
			button->setProperty("dmbCarvedFrom", QString());
		return;
	}
	// a plain setText after the word was carved (a download's progress, a language switch) is the button's new word: the text wins over the stored one
	if(stored.isValid() && !button->text().isEmpty() && button->text() != original)
		original = button->text();
	if(!stored.isValid() || stored.toString() != original)
		button->setProperty("dmbCarvedFrom", original);
	// DMB: the button's own icon, kept on first sight, so the plain word gets it back
	if(!button->property("dmbOwnIcon").isValid() && !button->property("dmbCarvedIcon").toBool())
	{
		button->setProperty("dmbOwnIcon", QVariant::fromValue(button->icon()));
		button->setProperty("dmbOwnIconSize", button->iconSize());
	}
	button->setProperty("dmbCarvedWidth", button->width());
	const double ratio = button->devicePixelRatioF();
	const double room = button->width() - 2 * (carvedBarEnds() + carvedClear);
	QString label;
	double scale = 0;
	if(active && button->width() > 0)
	{
		label = carvedStacked(original, room);
		const double natural = carvedWordWidth(label);
		// a label that does not fit even stacked asks its button for the room (the layout may give it; the button is carved again when it does)
		const int needed = static_cast<int>(std::ceil(natural + 4.0)) + 2 * (carvedBarEnds() + carvedClear);
		if(natural > 0 && needed > button->width() + 1)
		{
			if(button->maximumWidth() < needed)
				button->setMaximumWidth(needed); // the form capped the button at a plain word's width
			if(button->minimumWidth() < needed)
				button->setMinimumWidth(needed);
		}
		if(natural > 0)
		{
			scale = std::min(1.0, room / (natural + 4.0)); // the picture's margin included
			if(scale < CARVED_CONDENSE_FLOOR)
				scale = 0;
		}
	}
	const QImage enabled = scale > 0 ? carvedButtonWord(label, ratio, scale, lightLettering, true) : QImage();
	const bool stacked = !enabled.isNull() && label.contains(QLatin1Char('\n'));
	const bool fits = !enabled.isNull() && (stacked || logicalSize(enabled).height() + carvedClear <= button->height())
		&& logicalSize(enabled).width() + 2 * (carvedBarEnds() + carvedClear) <= button->width() + 1;
	button->setProperty("dmbCarvedWordWidth", fits ? logicalSize(enabled).width() : 0);
	// the bar's own height: two lines ask for the tall bar
	const int lines = fits && stacked ? 2 : 1;
	// a button the form made as high as one line (a fixed height) is let grow for two, and set back when it is one line again
	if(!button->property("dmbMaxHeight").isValid())
		button->setProperty("dmbMaxHeight", button->maximumHeight());
	button->setMaximumHeight(lines == 2 ? QWIDGETSIZE_MAX : button->property("dmbMaxHeight").toInt());
	if(button->property("dmbLines").toInt() != lines)
	{
		button->setProperty("dmbLines", lines);
		button->style()->unpolish(button);
		button->style()->polish(button);
		button->updateGeometry();
	}
	if(fits)
	{
		QIcon icon;
		icon.addPixmap(QPixmap::fromImage(enabled), QIcon::Normal);
		icon.addPixmap(QPixmap::fromImage(carvedButtonWord(label, ratio, scale, lightLettering, false)), QIcon::Disabled);
		button->setIcon(icon);
		button->setIconSize(logicalSize(enabled));
		button->setProperty("dmbCarvedIcon", true);
		button->setText(QString());
		// Qt took the accelerator from the "&" of the text: it stays on the button
		const QKeySequence mnemonic = QKeySequence::mnemonic(original);
		if(!mnemonic.isEmpty())
			button->setShortcut(mnemonic);
	}
	else if(button->property("dmbCarvedIcon").toBool())
	{
		// DMB: the carved picture goes whenever it is there, also beside a new plain text it could not carve, and the button's own
		// icon comes back. setText derives the accelerator from the word's "&" itself, so nothing clears it afterwards (that took Alt+P
		// from "&Play")
		button->setIcon(button->property("dmbOwnIcon").value<QIcon>());
		button->setIconSize(button->property("dmbOwnIconSize").toSize());
		button->setProperty("dmbCarvedIcon", false);
		if(button->text().isEmpty())
			button->setText(original);
	}
}

void carveEveryButtonUnder(QWidget * window, bool active)
{
	for(QPushButton * button : window->findChildren<QPushButton *>())
		applyCarvedButtonWord(button, active);
}

/// `applyCarvedButtonWord` on every button of every open window (a theme switch: a widget's own layout,
/// and so its width, is already real by then, every window having shown at least once already)
void carveEveryButton(bool active)
{
	for(QWidget * top : QApplication::topLevelWidgets())
		carveEveryButtonUnder(top, active);
}

/// What each of the launcher's four slots holds. "default" is the slot's own vanilla (leather, gold, gold, blue);
/// a look gives the slots their starting choices, and a player's choices (launcher.look.*) replace them.
struct LookSlots
{
	std::string background = "default";
	std::string partitions = "default";
	std::string primary = "default";
	std::string secondary = "default";
	bool dark = false;
};

bool knownColour(const std::string & colour)
{
	return colour == "default" || std::find(SLOT_COLOUR_NAMES.begin(), SLOT_COLOUR_NAMES.end(), colour) != SLOT_COLOUR_NAMES.end();
}

LookSlots lookSlots(const std::string & theme)
{
	LookSlots look_;
	look_.dark = theme == "dark";
	if(look_.dark)
	{
		// the dark look as it has been: silver edges and buttons, or the game's gold for the player who kept it
		// (launcher.darkAccent, the setting this replaces), and K's red in the place of the blue (October 1st)
		const bool gold = settings["launcher"]["darkAccent"].String() == "gold";
		look_.partitions = look_.primary = gold ? "default" : "silver";
		look_.secondary = "red";
	}
	const JsonNode & look = settings["launcher"]["look"];
	const auto choose = [&look](const char * key, std::string & slot)
	{
		if(look[key].isString() && !look[key].String().empty() && knownColour(look[key].String()))
			slot = look[key].String();
	};
	choose("background", look_.background);
	choose("partitions", look_.partitions);
	choose("primary", look_.primary);
	choose("secondary", look_.secondary);
	return look_;
}

/// how much of its brightness a hero colour keeps in a slot: the dark look's buttons and switches are darker
double slotWeight(const LookSlots & look_, const std::string & colour, double dark)
{
	return look_.dark && colour != "silver" && colour != "black" ? dark : 1.0;
}

/// vanilla colour, and the grey it becomes in silver (the dark look's, as K approved it, September 27th)
const std::vector<std::pair<const char *, const char *>> & vanillaColours()
{
	static const std::vector<std::pair<const char *, const char *>> colours = {
		{"#f3d27a", "#c6cad1"}, {"#d9a83e", "#9fa4ac"}, {"#9c6d1c", "#686c73"}, {"#fbe29a", "#d9dce1"},
		{"#e8bc55", "#b2b6bd"}, {"#ae7e2a", "#7b7f86"}, {"#8a6a2a", "#5c6067"}, {"#4a3008", "#2f3237"},
		{"#3a2a12", "#2a2c30"}, {"#7d6a4c", "#686b70"}, {"#1c1307", "#1a1a1b"}, {"#2b1d08", "#2a2b2d"},
		{"#24170a", "#202122"}, {"#1a1108", "#171718"}, {"#2a1a0a", "#1e1e1f"}, {"#e8c86a", "#d7dbe1"},
		{"#fff0b0", "#ffffff"}, {"#2a1c0c", "#222326"}, {"#3a2410", "#2c2c2c"},
	};
	return colours;
}

/// a vanilla colour of the launcher's gold-and-brown palette as `colour`
QColor recoloured(const QColor & vanilla, const std::string & colour, double weight)
{
	if(colour == "default")
		return vanilla;
	if(colour == "silver")
		for(const auto & [gold, silver] : vanillaColours())
			if(QColor(gold) == vanilla)
				return QColor(silver);
	return QColor(tintPixel(qGray(vanilla.rgb()), colour, weight));
}

/// a style sheet's gold-and-brown colours as `colour`
QString inColour(QString sheet, const std::string & colour, double weight)
{
	if(colour == "default")
		return sheet;
	for(const auto & [vanilla, silver] : vanillaColours())
	{
		(void)silver;
		// through a marker, so a colour once replaced is never replaced again by a later one
		sheet.replace(vanilla, "@" + recoloured(QColor(vanilla), colour, weight).name().mid(1) + "@", Qt::CaseInsensitive);
	}
	static const QRegularExpression marked("@([0-9a-fA-F]{6})@");
	sheet.replace(marked, "#\\1");
	return sheet;
}

/// the light text of the look: parchment, or the cool white the silver dark look has had since September 27th
QString textColour(const LookSlots & look_)
{
	return look_.dark && !(look_.partitions == "default" && look_.primary == "default") ? "#e8eaed" : "#f3e7c9";
}

/// a colour dark enough that black lettering on it cannot be read
bool needsLightLettering(const std::string & colour)
{
	return colour == "black";
}

struct ButtonStyles
{
	QString primary;
	QString secondary;
};

/// The launcher's buttons as the game's own gold bars (K, September 26th: "these buttons need to
/// actually mimic the heroes 3 buttons exactly"), from the player's own game files, written to the
/// user's cache for the style sheet to name, in the primary slot's colour (gold when it is the default). The Settings
/// page's categories are the same bars (K, September 27th). Empty before an import: then they keep the
/// painted gold. The switches are the Random Map Setup's blue choices, in the secondary slot's colour.
ButtonStyles gameButtonStyle(const LookSlots & look_)
{
	const AnimationPath path = AnimationPath::builtin("SPRITES/RANSHOW");
	if(!CResourceHandler::get()->existsResource(path))
		return {};
	const auto read = readResource(path);
	if(!read)
		return {};
	const auto & data = *read;
	const auto * bytes = reinterpret_cast<const ui8 *>(data.first.get());
	const std::string primary = look_.primary;
	const auto folder = VCMIDirs::get().userCachePath() / "launcher";
	boost::system::error_code ec;
	boost::filesystem::create_directories(folder, ec);
	const auto write = [&folder](const QImage & image, const std::string & name)
	{
		const QString file = QString::fromStdString((folder / (name + ".png")).string());
		return image.save(file, "PNG") ? QDir::fromNativeSeparators(file) : QString();
	};
	const std::string metal = (primary == "default" ? "" : "-" + primary) + (look_.dark && primary != "default" ? "-dark" : "");
	// the gold bar at `height` (one line of lettering, or two), in the primary slot's colour: its normal, pressed, lit and dimmed pictures written to
	// the cache ("" for the one line bar, "2" for the tall one in the file names), and the width of its scrollwork ends
	struct Bars
	{
		QString files[4];
		int end = 0;
	};
	const auto bars = [&](int height, const std::string & size) -> Bars
	{
		// every height has the one line bar's ornaments (tallBar), not the game's frame scaled up
		const auto bar = [&](int frame) { return tallBar(wordlessBar(readH3DefFrame(bytes, data.second, frame), BAR_HEIGHT), height); };
		QImage normal = bar(0);
		QImage pressed = bar(1);
		QImage lit = bar(3);
		if(normal.isNull() || pressed.isNull() || lit.isNull())
			return {};
		if(primary != "default")
		{
			const double weight = slotWeight(look_, primary, 0.9);
			normal = tinted(normal, primary, weight);
			pressed = tinted(pressed, primary, weight);
			lit = tinted(lit, primary, weight);
		}
		// black lettering on a bar that is dark (black, or a deep red) cannot be read: light lettering on those
		if(size.empty())
		{
			long long total = 0, count = 0;
			for(int y = 0; y < normal.height(); ++y)
			{
				const auto * line = reinterpret_cast<const QRgb *>(normal.constScanLine(y));
				for(int x = 0; x < normal.width(); ++x)
					if(qAlpha(line[x]) > 200)
					{
						total += qGray(line[x]);
						++count;
					}
			}
			lightLettering = count > 0 && total / count < 105;
		}
		// a button that cannot be pressed: the normal bar, dimmed
		QImage dimmed = normal;
		for(int y = 0; y < dimmed.height(); ++y)
		{
			auto * line = reinterpret_cast<QRgb *>(dimmed.scanLine(y));
			for(int x = 0; x < dimmed.width(); ++x)
				line[x] = qRgba(qRed(line[x]) * 55 / 100, qGreen(line[x]) * 50 / 100, qBlue(line[x]) * 45 / 100, qAlpha(line[x]));
		}
		Bars out;
		out.files[0] = write(normal, "dmb-button" + size + metal);
		out.files[1] = write(pressed, "dmb-button" + size + "-pressed" + metal);
		out.files[2] = write(lit, "dmb-button" + size + "-lit" + metal);
		out.files[3] = write(dimmed, "dmb-button" + size + "-disabled" + metal);
		for(const auto & file : out.files)
			if(file.isEmpty())
				return {};
		// the scrollwork ends keep their size, the middle stretches to the button
		out.end = normal.width() * 21 / (2 * 21 + 64);
		return out;
	};
	const Bars one = bars(BAR_HEIGHT, "");
	const Bars two = bars(STACK_BAR_HEIGHT, "2");
	if(one.end == 0 || two.end == 0)
		return {};
	const auto face = [](const Bars & set, int picture)
	{
		return QString("border-image: url(\"%1\") 4 %2 4 %2 stretch stretch;").arg(set.files[picture], QString::number(set.end)); // DMB: one pass, so a "%1" in the cache path is not substituted again
	};
	const int end = one.end;
	const QString ink = lightLettering ? textColour(look_) : "black";
	ButtonStyles styles;
	styles.primary = QString(
		"QPushButton { color: %6; background: transparent; border-radius: 0; border-width: 4px %1px; padding: 1px 2px; %2 }"
		"QPushButton:hover { %3 }"
		"QPushButton:pressed { %4 }"
		// Gemini's read of K's launcher screenshot, relayed by K overnight into September 28th: "Import
		// from Clipboard" grayed out was dark text on the dimmed bar's own dark gold, hard to read. A
		// light parchment tone (the same one the dark look already uses in gold's place) reads clearly
		// against the dimmed bar in either look, where the near-black original barely stood out from it.
		"QPushButton:disabled { color: #f3e7c9; %5 }"
		// the Settings page's categories: the same bars, the open one pressed in
		"#dmbSettingsCategories QPushButton { color: %6; background: transparent; border-radius: 0; border-width: 4px %1px;"
		" padding: 3px 6px; text-align: center; font-weight: bold; %2 }"
		"#dmbSettingsCategories QPushButton:hover { color: %6; background: transparent; %3 }"
		"#dmbSettingsCategories QPushButton:checked { color: %6; background: transparent; %4 }")
			.arg(end).arg(face(one, 0), face(one, 2), face(one, 1), face(one, 3), ink);
	// a label on two lines: the same bars, drawn tall (set by applyCarvedButtonWord's dmbLines property), after the rules above so they win
	styles.primary += QString(
		"QPushButton[dmbLines=\"2\"] { border-width: 4px %1px; %2 }"
		"QPushButton[dmbLines=\"2\"]:hover { %3 }"
		"QPushButton[dmbLines=\"2\"]:pressed { %4 }"
		"QPushButton[dmbLines=\"2\"]:disabled { %5 }"
		"#dmbSettingsCategories QPushButton[dmbLines=\"2\"] { border-width: 4px %1px; %2 }"
		"#dmbSettingsCategories QPushButton[dmbLines=\"2\"]:hover { %3 }"
		"#dmbSettingsCategories QPushButton[dmbLines=\"2\"]:checked { %4 }")
		.arg(two.end).arg(face(two, 0), face(two, 2), face(two, 1), face(two, 3));

	// the Settings page's switches as the Random Map Setup's choices: blue, and framed when on;
	// their marbled middle repeats rather than stretching
	const AnimationPath choicePath = AnimationPath::builtin("SPRITES/RANWEAK");
	if(!CResourceHandler::get()->existsResource(choicePath))
		return styles;
	const auto choiceRead = readResource(choicePath);
	if(!choiceRead)
		return styles;
	const auto & choiceData = *choiceRead;
	const auto * choiceBytes = reinterpret_cast<const ui8 *>(choiceData.first.get());
	const std::string secondary = look_.secondary;
	// the blue choice art is dark in lightness, so silver on it would read near black: lifted (K's other colours get their
	// own range, which does that)
	const double secondaryWeight = secondary == "silver" ? 2.6 : slotWeight(look_, secondary, SECONDARY_DARK_WEIGHT);
	const auto choice = [&](int frame)
	{
		const QImage art = wordlessChoice(readH3DefFrame(choiceBytes, choiceData.second, frame));
		return secondary == "default" ? art : tinted(art, secondary, secondaryWeight);
	};
	const std::string tone = (secondary == "default" ? "" : "-" + secondary) + (look_.dark && secondary != "default" ? "-dark" : "");
	const QString off = write(choice(0), "dmb-choice" + tone);
	const QString on = write(choice(3), "dmb-choice-on" + tone);
	if(off.isEmpty() || on.isEmpty())
		return styles;
	const QString text = textColour(look_);
	styles.secondary = QString(
		"CSettingsView QToolButton { color: %3; background: transparent; border-radius: 0; border-width: 4px 6px; padding: 1px 4px;"
		" border-image: url(\"%1\") 4 6 4 6 repeat stretch; }"
		"CSettingsView QToolButton:checked { color: #ffffff; background: transparent; border-image: url(\"%2\") 4 6 4 6 repeat stretch; }"
		"CSettingsView QToolButton:hover { color: #ffffff; }"
		"CSettingsView QToolButton:disabled { color: #8a8a8a; }")
		.arg(off, on, text);
	return styles;
}

/// The background slot: the game's leather as it is, or as `colour` (the game's player colours do the same to it for
/// a player's windows). The dark look has its own: the leather tinted toward black (K, September 26th: one harvested
/// texture in two tints, not a second dark design), the grain staying and the brown mostly going; a colour chosen
/// there is the same grain in that colour, dark.
QPixmap backgroundOf(const QPixmap & leather, const LookSlots & look_)
{
	QImage image = leather.toImage().convertToFormat(QImage::Format_RGB32);
	if(look_.background != "default")
	{
		// dark: silver stays a visible grey and black stays black, the colours at under half their brightness
		const double weight = !look_.dark ? 1.0 : look_.background == "silver" ? 0.9 : look_.background == "black" ? 0.4 : 0.45;
		return QPixmap::fromImage(tinted(image, look_.background, weight).convertToFormat(QImage::Format_RGB32));
	}
	if(!look_.dark)
		return leather;
	for(int y = 0; y < image.height(); ++y)
	{
		auto * line = reinterpret_cast<QRgb *>(image.scanLine(y));
		for(int x = 0; x < image.width(); ++x)
		{
			// two thirds of the way to grey, then down to 30% of the brightness
			const int grey = qGray(line[x]);
			const auto dim = [grey](int channel) { return (channel + 2 * grey) / 3 * 30 / 100; };
			line[x] = qRgb(dim(qRed(line[x])), dim(qGreen(line[x])), dim(qBlue(line[x])));
		}
	}
	return QPixmap::fromImage(image);
}

const QColor gold(217, 168, 62);

// Gold buttons with black text, as the game draws its own, and the Settings page's categories gold on black down
// its left. In three parts, one for each slot of the look that colours them (the background is the window's own
// brush): the primary buttons; the partitions, the side menu, tabs, frames and dropdowns; the secondary buttons,
// the Settings page's switches.
const char * primaryStyle = R"(
QPushButton {
	color: black;
	background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f3d27a, stop:0.5 #d9a83e, stop:1 #9c6d1c);
	border: 1px solid #4a3008;
	border-radius: 3px;
	padding: 4px 12px;
}
QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fbe29a, stop:0.5 #e8bc55, stop:1 #ae7e2a); }
QPushButton:pressed { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #9c6d1c, stop:1 #d9a83e); }
QPushButton:disabled { color: #3a2a12; background: #7d6a4c; border-color: #3a2a12; }
#dmbSettingsCategories QPushButton {
	color: #d9a83e;
	background: black;
	border-image: none;
	border: 1px solid #4a3008;
	border-radius: 0;
	padding: 6px 14px;
	text-align: left;
	font-weight: bold;
}
#dmbSettingsCategories QPushButton:hover { color: #fbe29a; background: #1c1307; }
#dmbSettingsCategories QPushButton:checked { color: #fbe29a; background: #2b1d08; border: 1px solid #d9a83e; }
)";

const char * partitionsStyle = R"(
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; }
QToolButton:checked, QToolButton:hover { background: rgba(0, 0, 0, 90); border: 1px solid #d9a83e; }
QTabBar::tab { padding: 4px 14px; border: 1px solid #8a6a2a; border-bottom: none; }
QTabBar::tab:selected { color: black; background: #d9a83e; }
QHeaderView::section { padding: 3px 6px; border: none; border-right: 1px solid #8a6a2a; border-bottom: 1px solid #8a6a2a; }
QToolTip { color: #f3e7c9; background: #2a1a0a; border: 1px solid #d9a83e; }
QProgressBar { border: 1px solid #8a6a2a; text-align: center; }
QProgressBar::chunk { background: #d9a83e; }
#dmbSettingsPanel { border: 2px solid #d9a83e; border-radius: 3px; }
QLabel[dmbSettingsTitle="true"] { color: #d9a83e; font-size: 13pt; font-weight: bold; }
CSettingsView QComboBox { border: 1px solid #d9a83e; border-radius: 3px; padding: 3px 8px; }
CSettingsView QComboBox:hover { border-color: #fbe29a; }
CSettingsView QComboBox QAbstractItemView { border: 1px solid #d9a83e; selection-color: black; selection-background-color: #d9a83e; }
)";

// the dropdowns' own ground: dark, in the colour of the window's background (or of the partitions when the background keeps
// the game's own leather, so the dark look's silver dropdowns stay)
const char * fieldsStyle = R"(
CSettingsView QComboBox { color: #f3e7c9; background: #24170a; }
CSettingsView QComboBox QAbstractItemView { color: #f3e7c9; background: #1a1108; }
)";

const char * secondaryStyle = R"(
CSettingsView QToolButton { color: #f3e7c9; background: #24170a; border: 1px solid #8a6a2a; border-radius: 3px; padding: 3px 10px; }
CSettingsView QToolButton:hover { border-color: #d9a83e; }
CSettingsView QToolButton:checked {
	color: black;
	background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f3d27a, stop:0.5 #d9a83e, stop:1 #9c6d1c);
	border: 1px solid #4a3008;
}
)";

QPalette themedPalette(const QBrush & window, const QColor & base, const QColor & alternate, const QColor & button,
	const QColor & accent, const QColor & text, const QColor & highlighted)
{
	QPalette palette;
	palette.setBrush(QPalette::Window, window);
	palette.setColor(QPalette::WindowText, text);
	palette.setColor(QPalette::Base, base);
	palette.setColor(QPalette::AlternateBase, alternate);
	palette.setColor(QPalette::Text, text);
	palette.setColor(QPalette::PlaceholderText, QColor(170, 150, 110));
	palette.setColor(QPalette::Button, button);
	palette.setColor(QPalette::ButtonText, text);
	palette.setColor(QPalette::BrightText, accent);
	palette.setColor(QPalette::Highlight, accent);
	palette.setColor(QPalette::HighlightedText, highlighted);
	palette.setColor(QPalette::Link, accent);
	palette.setColor(QPalette::ToolTipBase, QColor(42, 26, 10));
	palette.setColor(QPalette::ToolTipText, text);
	palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(150, 135, 105));
	palette.setColor(QPalette::Disabled, QPalette::Text, QColor(150, 135, 105));
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(150, 135, 105));
	return palette;
}

/// WCAG relative luminance, and the contrast ratio of two colours (1 for the same, 21 for black on white)
double luminanceOf(const QColor & colour)
{
	const auto channel = [](double value)
	{
		return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
	};
	return 0.2126 * channel(colour.redF()) + 0.7152 * channel(colour.greenF()) + 0.0722 * channel(colour.blueF());
}

double contrastOf(const QColor & a, const QColor & b)
{
	const double la = luminanceOf(a), lb = luminanceOf(b);
	return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/// the average colour of a brush's picture (or its plain colour): the ground the slots' colours are read against
QColor groundOf(const QBrush & window)
{
	const QImage image = window.textureImage();
	if(image.isNull())
		return window.color();
	long long red = 0, green = 0, blue = 0, count = 0;
	for(int y = 0; y < image.height(); y += 8)
		for(int x = 0; x < image.width(); x += 8)
		{
			const QRgb pixel = image.pixel(x, y);
			red += qRed(pixel);
			green += qGreen(pixel);
			blue += qBlue(pixel);
			++count;
		}
	return count == 0 ? window.color() : QColor(static_cast<int>(red / count), static_cast<int>(green / count), static_cast<int>(blue / count));
}

/// `ink` as it is when it stands out of `ground` by `ratio`, else lightened (on a dark ground) or darkened (on a light one)
/// toward white or black until it does: black partitions on a silver background, or a deep red on black, stay the
/// colour the player picked wherever it can be seen, and are the nearest visible tone of it where it cannot
QColor legible(const QColor & ink, const QColor & ground, double ratio)
{
	if(contrastOf(ink, ground) >= ratio)
		return ink;
	const QColor end = luminanceOf(ground) < 0.4 ? QColor(255, 255, 255) : QColor(0, 0, 0);
	for(int step = 1; step <= 20; ++step)
	{
		const double t = step / 20.0;
		const QColor mixed(static_cast<int>(ink.red() + (end.red() - ink.red()) * t), static_cast<int>(ink.green() + (end.green() - ink.green()) * t),
			static_cast<int>(ink.blue() + (end.blue() - ink.blue()) * t));
		if(contrastOf(mixed, ground) >= ratio)
			return mixed;
	}
	return end;
}

/// the launcher's whole style sheet and palette for `slots`; `leather` is null before the game's files are imported
struct AppliedLook
{
	QString sheet;
	QPalette palette;
};

AppliedLook buildLook(const LookSlots & look_, const QPixmap & leather)
{
	const QString text = textColour(look_);
	const auto lighten = [&text](QString sheet, const std::string & colour)
	{
		// black lettering and black selections on a black slot cannot be read: the look's light text instead
		if(needsLightLettering(colour))
			sheet.replace("color: black", "color: " + text);
		return sheet;
	};
	const double partitionsWeight = slotWeight(look_, look_.partitions, 0.85);
	const double secondaryWeight = slotWeight(look_, look_.secondary, SECONDARY_DARK_WEIGHT);
	const double primaryWeight = slotWeight(look_, look_.primary, 0.9);

	const ButtonStyles game = gameButtonStyle(look_);
	QString sheet = lighten(inColour(primaryStyle, look_.primary, primaryWeight), look_.primary) + game.primary;
	sheet += lighten(inColour(partitionsStyle, look_.partitions, partitionsWeight), look_.partitions);
	const bool fieldsFollowBackground = look_.background != "default";
	sheet += inColour(fieldsStyle, fieldsFollowBackground ? look_.background : look_.partitions,
		fieldsFollowBackground ? slotWeight(look_, look_.background, 0.5) : partitionsWeight);
	sheet += lighten(inColour(secondaryStyle, look_.secondary, secondaryWeight), look_.secondary) + game.secondary;

	// tabs and column headers: a dark ground in the partitions' colour
	QString tabGround = look_.dark ? "#2c2c2c" : "#3a2410";
	if(look_.partitions != "default" && look_.partitions != "silver")
		tabGround = look_.dark ? QColor(tintPixel(44, look_.partitions, partitionsWeight)).name()
			: recoloured(QColor("#3a2410"), look_.partitions, partitionsWeight).name();
	else if(look_.partitions == "silver" && !look_.dark)
		tabGround = "#2c2c2c";
	sheet += QString("QTabBar::tab, QHeaderView::section { color: %1; background: %2; }").arg(text, tabGround);
	// the platform's own parchment text, and the light text this look uses in its place
	sheet.replace("#f3e7c9", text, Qt::CaseInsensitive);

	// lists and text fields; the same ground as the window, a step darker
	const QColor base = look_.dark ? QColor(16, 16, 16) : QColor(34, 22, 10);
	const QColor alternate = look_.dark ? QColor(30, 30, 30) : QColor(46, 30, 14);
	const QColor button = look_.dark ? QColor(44, 44, 44) : QColor(70, 46, 20);
	const auto ground = [&look_](const QColor & vanilla)
	{
		return look_.background == "default" ? vanilla : recoloured(vanilla, look_.background, 1.0);
	};
	QBrush window;
	if(!leather.isNull())
		window = QBrush(backgroundOf(leather, look_));
	else if(look_.background == "default")
		window = QBrush(QColor(24, 24, 24));
	else
		window = QBrush(QColor(tintPixel(60, look_.background, 0.4)));
	const QColor accent = recoloured(gold, look_.partitions, partitionsWeight);
	const QColor highlighted = accent.lightness() < 90 ? QColor(text) : QColor(Qt::black);
	// the Settings page's heading and the frames of its panel and dropdowns are the partitions' colour read against the
	// window's own ground: where that colour cannot be seen on it (black partitions on a silver background, a deep red
	// heading on black) they are its nearest visible tone, and elsewhere unchanged
	const QColor windowGround = groundOf(window);
	const QColor titleInk = legible(accent, windowGround, 4.5);
	const QColor frameInk = legible(accent, windowGround, 1.8);
	if(titleInk != accent)
		sheet += QString("QLabel[dmbSettingsTitle=\"true\"] { color: %1; }").arg(titleInk.name());
	if(frameInk != accent)
		sheet += QString("#dmbSettingsPanel { border-color: %1; } CSettingsView QComboBox { border-color: %1; }"
			" CSettingsView QComboBox QAbstractItemView { border-color: %1; }").arg(frameInk.name());
	// the sliders of the Settings pages draw their handle and the empty part of the groove from the palette's button colour, which a
	// black background makes black: both vanished on K's own look (October 3rd bug hunt, Audio page). Where the button colour does
	// not stand out of the ground, the handle and the groove are the nearest tone that does, and the filled part keeps the accent.
	if(contrastOf(ground(button), windowGround) < 1.6)
	{
		const QColor grooveInk = legible(ground(button), windowGround, 1.7);
		const QColor handleInk = legible(ground(button), windowGround, 3.0);
		sheet += QString("QSlider::groove:horizontal { height: 4px; background: %1; border-radius: 2px; }"
			" QSlider::sub-page:horizontal { height: 4px; background: %2; border-radius: 2px; }"
			" QSlider::handle:horizontal { width: 10px; margin: -5px 0; background: %3; border: 1px solid %2; border-radius: 2px; }")
			.arg(grooveInk.name(), frameInk.name(), handleInk.name());
	}
	AppliedLook look;
	look.sheet = sheet;
	look.palette = themedPalette(window, ground(base), ground(alternate), ground(button), accent, QColor(text), highlighted);
	return look;
}
}

// the kinds of widget Qt gives a platform font of their own (QApplication's widget font table)
const char * const WIDGETS_WITH_OWN_FONTS[] = { "QAbstractItemView", "QListView", "QHeaderView", "QMenu", "QMenuItem", "QMenuBar",
	"QTipLabel", "QMessageBox", "QStatusBar", "QComboMenuItem", "QComboLineEdit", "QLabel", "QPushButton", "QCheckBox",
	"QRadioButton", "QToolButton", "QTabBar", "QMdiSubWindowTitleBar", "QDockWidgetTitle", "QSmallFont", "QMiniFont" };

/// The game's own fonts for the leather and dark looks (gamefont.h): its MEDFONT for text and BIGFONT
/// for titles, each at the one size that draws the game's pixels exactly, without smoothing, as the
/// game draws them. Empty before the game's files are imported: the platform's font stays.
static QString gameFontStyle()
{
	const GameFont::Font text = GameFont::load("MEDFONT", "DMB Heroes");
	const GameFont::Font title = GameFont::load("BIGFONT", "DMB Heroes Big");
	if(text.family.isEmpty())
		return {};
	QFont font(text.family);
	font.setPixelSize(text.height);
	font.setStyleStrategy(QFont::NoAntialias);
	QApplication::setFont(font);
	// the platform gives some kinds of widget fonts of their own (a list's column headers, menus,
	// tooltips, message boxes), which the application's font does not reach
	for(const char * kind : WIDGETS_WITH_OWN_FONTS)
		QApplication::setFont(font, kind);
	// the style sheet's bold and point sizes would smear the game's pixels: its own weight and size
	QString style = QString(
		"#dmbSettingsCategories QPushButton { font-weight: normal; }"
		"QLabel[dmbSettingsTitle=\"true\"] { font-weight: normal; font-size: %1px; }"
		// lists and their column headers keep a platform font of their own otherwise
		"QAbstractItemView, QHeaderView::section { font-family: \"%2\"; font-size: %1px; }").arg(text.height).arg(text.family);
	if(!title.family.isEmpty())
		style += QString("QLabel[dmbSettingsTitle=\"true\"] { font-family: \"%1\"; font-size: %2px; }").arg(title.family).arg(title.height);
	return style;
}

void LauncherTheme::applySidebarIcon(QToolButton * button, const std::string & defName, int holeX, int holeY, int holeW, int holeH)
{
	const AnimationPath path = AnimationPath::builtin("SPRITES/" + defName);
	if(!CResourceHandler::get()->existsResource(path))
		return;
	const auto read = readResource(path);
	if(!read)
		return;
	const auto & data = *read;
	const auto * bytes = reinterpret_cast<const ui8 *>(data.first.get());
	// frame 3, the button's own "lit" colouring (wordlessBar reads the same frame for the gold bar
	// itself): the most saturated of the four, closest to what a hovered main-menu button looks like
	QImage frame = readH3DefFrame(bytes, data.second, 3).convertToFormat(QImage::Format_ARGB32);
	if(frame.isNull() || holeX + holeW > frame.width() || holeY + holeH > frame.height())
		return;
	// the whole button face, kept (never a crop that loses part of the image, K's standing rule this
	// file records above coonsPatchInpaint): its own baked-in word sits in (holeX, holeY, holeW, holeH)
	// and gets filled in from the real art surrounding it instead
	coonsPatchInpaint(frame, holeX, holeY, holeX + holeW, holeY + holeH);
	// the source is full painterly game art, several times a sidebar icon's own size; it reads as a
	// soft, low-contrast blob at 40-48px, so push contrast and saturation before it ships this small
	// (K's own screenshot review, September 28th)
	constexpr double contrast = 1.35;
	constexpr double saturation = 1.3;
	for(int y = 0; y < frame.height(); ++y)
	{
		auto * line = reinterpret_cast<QRgb *>(frame.scanLine(y));
		for(int x = 0; x < frame.width(); ++x)
		{
			const QRgb px = line[x];
			const double gray = (qRed(px) + qGreen(px) + qBlue(px)) / 3.0;
			const auto push = [&](int channel)
			{
				const double saturated = gray + (channel - gray) * saturation;
				return std::clamp(static_cast<int>(std::lround((saturated - 128.0) * contrast + 128.0)), 0, 255);
			};
			line[x] = qRgba(push(qRed(px)), push(qGreen(px)), push(qBlue(px)), qAlpha(px));
		}
	}
	button->setIcon(QIcon(QPixmap::fromImage(frame)));
}

void LauncherTheme::setButtonText(QPushButton * button, const QString & text)
{
	const QVariant stored = button->property("dmbCarvedFrom");
	if(!stored.isValid())
	{
		button->setText(text); // never seen by the carving: a plain button
		return;
	}
	// carved means the picture is all the button shows: a plain setText since (a download's progress, say) put text beside it, and then the
	// text asked for here is set, so it wins over that text in applyCarvedButtonWord
	const bool carved = button->property("dmbCarvedIcon").toBool() && button->text().isEmpty();
	if(carved && stored.toString() == text)
		return; // the word it already carries
	button->setProperty("dmbCarvedFrom", text);
	if(!carved)
		button->setText(text);
	applyCarvedButtonWord(button, darkFrames);
}

int LauncherTheme::minimumCarvedButtonWidth(const QString & text)
{
	// the room the label needs with nothing squeezed: its two line stacking where it has several words, the whole word where it has one
	const QString stacked = carvedStacked(text, 0);
	const double natural = carvedWordWidth(stacked);
	if(natural <= 0)
		return 0;
	return static_cast<int>(std::ceil(natural + 4)) + 2 * (carvedBarEnds() + carvedClear);
}

void LauncherTheme::apply()
{
	// the platform's own look, kept from the first call, for switching back to "system" from the Settings page
	static const QString systemStyle = QApplication::style()->objectName(); // Qt 5 keeps the style's key there
	static const QPalette systemPalette = QApplication::palette();
	static const QFont systemFont = QApplication::font();
	static const std::map<std::string, QFont> systemFonts = []()
	{
		std::map<std::string, QFont> fonts;
		for(const char * kind : WIDGETS_WITH_OWN_FONTS)
			fonts[kind] = QApplication::font(kind);
		return fonts;
	}();
	static FrameEveryWindow * const framer = []()
	{
		auto * filter = new FrameEveryWindow();
		qApp->installEventFilter(filter);
		return filter;
	}();
	(void)framer;

	const std::string theme = settings["launcher"]["theme"].String();
	// the title bars of windows already open follow at once; later ones as they show
	darkFrames = theme != "system";
	for(QWidget * window : QApplication::topLevelWidgets())
		if(window->isVisible())
			frameWindow(window);

	if(theme == "system")
	{
		QApplication::setStyle(QStyleFactory::create(systemStyle));
		QApplication::setPalette(systemPalette);
		QApplication::setFont(systemFont);
		for(const auto & [kind, font] : systemFonts)
			QApplication::setFont(font, kind.c_str());
		qApp->setStyleSheet(QString());
		carveEveryButton(false);
		logGlobal->info("Launcher look: system");
		return;
	}

	const QPixmap leather = playerLeather();

	QApplication::setStyle(QStyleFactory::create("Fusion")); // the one style that follows the palette everywhere
	// the leather look needs the game's leather; before the files are imported it is the dark look, flat
	const bool useLeather = theme == "leather" && !leather.isNull();
	const LookSlots look_ = lookSlots(useLeather ? "leather" : "dark");
	lightLettering = needsLightLettering(look_.primary);
	const AppliedLook look = buildLook(look_, leather);
	QApplication::setPalette(look.palette);
	qApp->setStyleSheet(look.sheet + gameFontStyle());
	logGlobal->info("Launcher look: %s%s; background %s, partitions %s, primary buttons %s, secondary buttons %s",
		useLeather ? "leather" : "dark", leather.isNull() ? " (flat, no game files to read the leather from yet)" : "",
		look_.background, look_.partitions, look_.primary, look_.secondary);
	carveEveryButton(true);
}

std::vector<std::string> LauncherTheme::slotColours()
{
	std::vector<std::string> colours = {"default"};
	colours.insert(colours.end(), SLOT_COLOUR_NAMES.begin(), SLOT_COLOUR_NAMES.end());
	return colours;
}

std::string LauncherTheme::slotColour(const std::string & slot)
{
	const LookSlots look_ = lookSlots(settings["launcher"]["theme"].String() == "dark" ? "dark" : "leather");
	if(slot == "background")
		return look_.background;
	if(slot == "partitions")
		return look_.partitions;
	if(slot == "primary")
		return look_.primary;
	return look_.secondary;
}

unsigned LauncherTheme::slotSwatch(const std::string & slot, const std::string & colour)
{
	if(colour == "default")
	{
		if(slot == "background")
			return 0x5a3c1e;
		return slot == "secondary" ? 0x284696 : 0xd9a83e;
	}
	if(colour == "silver")
		return 0xa0a4ac;
	if(colour == "black")
		return 0x181818;
	return tintPixel(slot == "background" ? 90 : 150, colour, 1.0) & 0xFFFFFF;
}
