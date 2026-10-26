/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Theme.
 *
 * Maps semantic interface roles to the openi2pd palette and allocates
 * terminal colour pairs. Support degrades from direct and 256-colour output
 * through 16- and 8-colour ANSI to monochrome attributes, keeping default
 * terminal colours when they are usable.
 *
 */

#ifndef OPENI2PD_TUI_THEME_H
#define OPENI2PD_TUI_THEME_H

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>

#include "Terminal.h"

/*
 * Central terminal theme for i2pd-tui.
 *
 * The palette is derived from assets/branding/colors.md.  The terminal is
 * never sent raw ANSI escapes by the shared code: every colour goes through
 * a terminal colour-pair attribute.  The module degrades in this order:
 *
 *   direct colour (terminal RGB indices + extended pairs)
 *   256-colour xterm cube
 *   16-colour ANSI
 *   8-colour ANSI
 *   monochrome attributes (reverse, bold, underline, dim)
 *
 * The brand colours are approximations on 8/16/256-colour terminals.  Plain
 * text stays on the terminal's default colours whenever default colours are
 * available and the background is known.  If it is not, an explicit
 * black/white background is chosen so text never becomes invisible.
 */

namespace i2p
{
namespace tui
{
	enum class ThemeRole : std::size_t
	{
		HeaderBar = 0,
		Title,
		StatusBar,
		SelectedRow,
		SelectedTab,
		PanelBorder,
		Primary,
		Secondary,
		Success,
		Warning,
		Error,
		Disabled,
		Muted,
		TorrentProgress,
		TorrentDownload,
		TorrentUpload,
		LogDebug,
		LogInfo,
		LogWarning,
		LogError,
		LogCritical,
		Count
	};

	namespace theme_detail
	{
		struct Rgb
		{
			int r = 0;
			int g = 0;
			int b = 0;
		};

		constexpr Rgb FromHex (unsigned int value)
		{
			return { static_cast<int> ((value >> 16) & 0xFFu), static_cast<int> ((value >> 8) & 0xFFu),
				static_cast<int> (value & 0xFFu) };
		}

		// Semantic tokens from assets/branding/colors.md.  The logo accents
		// are represented through these tokens rather than used as literal
		// text colours, because the sampled teal/blue fail normal-text
		// contrast on a light surface.
		constexpr Rgb DarkPrimary = FromHex (0x59D8D4);
		constexpr Rgb DarkPrimaryContainer = FromHex (0x00504E);
		constexpr Rgb DarkOnPrimaryContainer = FromHex (0xC8F5F2);
		constexpr Rgb DarkSecondary = FromHex (0x7CC7FF);
		constexpr Rgb DarkBackground = FromHex (0x0B141C);
		constexpr Rgb DarkOnSurface = FromHex (0xE4EDF4);
		constexpr Rgb DarkSurfaceVariant = FromHex (0x273746);
		constexpr Rgb DarkOnSurfaceVariant = FromHex (0xC3D1DC);
		constexpr Rgb DarkOutline = FromHex (0x91A2B1);
		constexpr Rgb DarkSelected = FromHex (0x123B3A);
		constexpr Rgb DarkDisabled = FromHex (0x4A5A69);
		constexpr Rgb DarkError = FromHex (0xFFB4AB);
		constexpr Rgb DarkWarning = FromHex (0xFFD48A);
		constexpr Rgb DarkSuccess = FromHex (0x7ADFA0);

		// Light semantic tokens.
		constexpr Rgb LightPrimary = FromHex (0x007C7A);
		constexpr Rgb LightOnPrimary = FromHex (0xFFFFFF);
		constexpr Rgb LightPrimaryContainer = FromHex (0xC8F5F2);
		constexpr Rgb LightOnPrimaryContainer = FromHex (0x003735);
		constexpr Rgb LightSecondary = FromHex (0x0067B1);
		constexpr Rgb LightBackground = FromHex (0xF7FAFC);
		constexpr Rgb LightOnSurface = FromHex (0x162737);
		constexpr Rgb LightSurfaceVariant = FromHex (0xDCE7EF);
		constexpr Rgb LightOnSurfaceVariant = FromHex (0x3F5263);
		constexpr Rgb LightOutline = FromHex (0x5C6E7E);
		constexpr Rgb LightSelected = FromHex (0xD6F4F1);
		constexpr Rgb LightDisabled = FromHex (0xA7B5C0);
		constexpr Rgb LightError = FromHex (0xB3261E);
		constexpr Rgb LightWarning = FromHex (0x8A5A00);
		constexpr Rgb LightSuccess = FromHex (0x1E6B45);

		struct RoleSpec
		{
			Rgb foreground;
			Rgb background;
			bool defaultForeground = false;
			bool defaultBackground = false;
		};

		inline RoleSpec SpecFor (ThemeRole role, bool light)
		{
			const Rgb onSurface = light ? LightOnSurface : DarkOnSurface;
			const Rgb onSurfaceVariant = light ? LightOnSurfaceVariant : DarkOnSurfaceVariant;
			const Rgb surfaceVariant = light ? LightSurfaceVariant : DarkSurfaceVariant;
			const Rgb outline = light ? LightOutline : DarkOutline;
			const Rgb primary = light ? LightPrimary : DarkPrimary;
			const Rgb primaryContainer = light ? LightPrimaryContainer : DarkPrimaryContainer;
			const Rgb onPrimaryContainer = light ? LightOnPrimaryContainer : DarkOnPrimaryContainer;
			const Rgb secondary = light ? LightSecondary : DarkSecondary;
			const Rgb selected = light ? LightSelected : DarkSelected;
			const Rgb disabled = light ? LightDisabled : DarkDisabled;
			const Rgb background = light ? LightBackground : DarkBackground;
			const Rgb error = light ? LightError : DarkError;
			const Rgb warning = light ? LightWarning : DarkWarning;
			const Rgb success = light ? LightSuccess : DarkSuccess;

			switch (role)
			{
			case ThemeRole::HeaderBar: return { onSurface, surfaceVariant };
			case ThemeRole::Title: return { onSurface, surfaceVariant };
			case ThemeRole::StatusBar: return { onSurfaceVariant, surfaceVariant };
			case ThemeRole::SelectedRow: return { onPrimaryContainer, selected };
			case ThemeRole::SelectedTab: return { onPrimaryContainer, primaryContainer };
			case ThemeRole::PanelBorder: return { outline, background, false, true };
			case ThemeRole::Primary: return { primary, background, false, true };
			case ThemeRole::Secondary: return { secondary, background, false, true };
			case ThemeRole::Success: return { success, background, false, true };
			case ThemeRole::Warning: return { warning, background, false, true };
			case ThemeRole::Error: return { error, background, false, true };
			case ThemeRole::Disabled: return { disabled, background, false, true };
			case ThemeRole::Muted: return { onSurfaceVariant, background, false, true };
			case ThemeRole::TorrentProgress: return { success, background, false, true };
			case ThemeRole::TorrentDownload: return { primary, background, false, true };
			case ThemeRole::TorrentUpload: return { secondary, background, false, true };
			case ThemeRole::LogDebug: return { onSurfaceVariant, background, false, true };
			case ThemeRole::LogInfo: return { onSurface, background, true, true };
			case ThemeRole::LogWarning: return { warning, background, false, true };
			case ThemeRole::LogError: return { error, background, false, true };
			case ThemeRole::LogCritical: return { light ? LightOnPrimary : background, error };
			case ThemeRole::Count: break;
			}
			return { onSurface, background, true, true };
		}

		struct AnsiSpec
		{
			int foreground = 0;
			int background = 0;
			bool defaultForeground = false;
			bool defaultBackground = false;
		};

		inline AnsiSpec AnsiFor (ThemeRole role, bool light, bool bright)
		{
			const int black = terminal::COLOR_BLACK;
			const int red = terminal::COLOR_RED;
			const int green = terminal::COLOR_GREEN;
			const int yellow = terminal::COLOR_YELLOW;
			const int blue = terminal::COLOR_BLUE;
			const int magenta = terminal::COLOR_MAGENTA;
			const int cyan = terminal::COLOR_CYAN;
			const int white = terminal::COLOR_WHITE;
			const int gray = bright ? terminal::COLOR_BLACK + 8 : white;
			const int brightRed = bright ? terminal::COLOR_RED + 8 : red;
			const int brightGreen = bright ? terminal::COLOR_GREEN + 8 : green;
			const int brightYellow = bright ? terminal::COLOR_YELLOW + 8 : yellow;
			const int brightBlue = bright ? terminal::COLOR_BLUE + 8 : blue;
			const int brightCyan = bright ? terminal::COLOR_CYAN + 8 : cyan;
			const int brightWhite = bright ? terminal::COLOR_WHITE + 8 : white;

			switch (role)
			{
			case ThemeRole::HeaderBar:
			case ThemeRole::Title:
			case ThemeRole::StatusBar: return { brightWhite, blue };
			case ThemeRole::SelectedRow:
			case ThemeRole::SelectedTab: return { black, brightCyan };
			case ThemeRole::PanelBorder: return { light ? blue : gray, 0, false, true };
			case ThemeRole::Primary: return { light ? brightBlue : brightCyan, 0, false, true };
			case ThemeRole::Secondary: return { light ? cyan : brightBlue, 0, false, true };
			case ThemeRole::Success: return { light ? green : brightGreen, 0, false, true };
			case ThemeRole::Warning:
				// 8/16-colour amber is not reproducible on a light terminal;
				// use default black with the bold warning label instead.
				return { light ? black : brightYellow, 0, false, true };
			case ThemeRole::Error: return { light ? red : brightRed, 0, false, true };
			case ThemeRole::Disabled: return { light ? black : gray, 0, false, true };
			case ThemeRole::Muted: return { light ? black : white, 0, false, true };
			case ThemeRole::TorrentProgress: return { light ? green : brightGreen, 0, false, true };
			case ThemeRole::TorrentDownload: return { light ? brightBlue : brightCyan, 0, false, true };
			case ThemeRole::TorrentUpload: return { light ? magenta : brightBlue, 0, false, true };
			case ThemeRole::LogDebug: return { light ? black : gray, 0, false, true };
			case ThemeRole::LogInfo: return { 0, 0, true, true };
			case ThemeRole::LogWarning: return { light ? yellow : brightYellow, 0, false, true };
			case ThemeRole::LogError: return { light ? red : brightRed, 0, false, true };
			case ThemeRole::LogCritical: return { brightWhite, light ? red : brightRed };
			case ThemeRole::Count: break;
			}
			return { 0, 0, true, true };
		}

		inline int MonoAttributes (ThemeRole role)
		{
			switch (role)
			{
			case ThemeRole::HeaderBar:
			case ThemeRole::StatusBar: return terminal::ATTR_REVERSE;
			case ThemeRole::Title:
			case ThemeRole::SelectedRow:
			case ThemeRole::SelectedTab:
			case ThemeRole::LogCritical: return terminal::ATTR_REVERSE | terminal::ATTR_BOLD;
			case ThemeRole::PanelBorder:
			case ThemeRole::Disabled:
			case ThemeRole::Muted:
			case ThemeRole::LogDebug: return terminal::ATTR_DIM;
			case ThemeRole::Primary:
			case ThemeRole::Secondary:
			case ThemeRole::Success:
			case ThemeRole::Warning:
			case ThemeRole::TorrentProgress:
			case ThemeRole::TorrentDownload:
			case ThemeRole::LogWarning: return terminal::ATTR_BOLD;
			case ThemeRole::Error:
			case ThemeRole::LogError: return terminal::ATTR_BOLD | terminal::ATTR_UNDERLINE;
			case ThemeRole::TorrentUpload: return terminal::ATTR_BOLD | terminal::ATTR_UNDERLINE;
			case ThemeRole::LogInfo:
			case ThemeRole::Count: break;
			}
			return terminal::ATTR_NORMAL;
		}

		inline int Emphasis (ThemeRole role)
		{
			switch (role)
			{
			case ThemeRole::Title:
			case ThemeRole::SelectedRow:
			case ThemeRole::SelectedTab:
			case ThemeRole::Primary:
			case ThemeRole::Secondary:
			case ThemeRole::Success:
			case ThemeRole::Warning:
			case ThemeRole::Error:
			case ThemeRole::TorrentProgress:
			case ThemeRole::TorrentDownload:
			case ThemeRole::LogWarning:
			case ThemeRole::LogError:
			case ThemeRole::LogCritical: return terminal::ATTR_BOLD;
			case ThemeRole::Disabled:
			case ThemeRole::Muted:
			case ThemeRole::LogDebug: return terminal::ATTR_DIM;
			case ThemeRole::HeaderBar:
			case ThemeRole::StatusBar:
			case ThemeRole::PanelBorder:
			case ThemeRole::TorrentUpload:
			case ThemeRole::LogInfo:
			case ThemeRole::Count: break;
			}
			return terminal::ATTR_NORMAL;
		}

		// Standard xterm values for the first 256 palette entries.
		inline bool PaletteRgb (int index, int & r, int & g, int & b)
		{
			static constexpr int ANSI[16][3] = { { 0, 0, 0 }, { 128, 0, 0 }, { 0, 128, 0 }, { 128, 128, 0 },
				{ 0, 0, 128 }, { 128, 0, 128 }, { 0, 128, 128 }, { 192, 192, 192 }, { 128, 128, 128 }, { 255, 0, 0 },
				{ 0, 255, 0 }, { 255, 255, 0 }, { 0, 0, 255 }, { 255, 0, 255 }, { 0, 255, 255 }, { 255, 255, 255 } };
			if (index < 0) return false;
			if (index < 16)
			{
				r = ANSI[index][0];
				g = ANSI[index][1];
				b = ANSI[index][2];
				return true;
			}
			if (index < 232)
			{
				const int value = index - 16;
				const int steps[6] = { 0, 95, 135, 175, 215, 255 };
				r = steps[(value / 36) % 6];
				g = steps[(value / 6) % 6];
				b = steps[value % 6];
				return true;
			}
			if (index < 256)
			{
				const int level = 8 + 10 * (index - 232);
				r = level;
				g = level;
				b = level;
				return true;
			}
			return false;
		}

		inline int DistanceSquared (const Rgb & colour, int r, int g, int b)
		{
			const int dr = colour.r - r;
			const int dg = colour.g - g;
			const int db = colour.b - b;
			return dr * dr + dg * dg + db * db;
		}

		inline int RgbTo256 (const Rgb & colour)
		{
			int best = 16;
			int bestDistance = 1 << 30;
			for (int index = 16; index < 256; ++index)
			{
				int r = 0, g = 0, b = 0;
				if (!PaletteRgb (index, r, g, b)) continue;
				const int distance = DistanceSquared (colour, r, g, b);
				if (distance < bestDistance)
				{
					bestDistance = distance;
					best = index;
				}
			}
			return best;
		}

		inline int PackRgb (const Rgb & colour)
		{
			return ((colour.r & 0xFF) << 16) | ((colour.g & 0xFF) << 8) | (colour.b & 0xFF);
		}

		// std::getenv is reported as deprecated by the MSVC CRT. Use the
		// bounds-checked _dupenv_s variant there and keep the portable call
		// elsewhere; an unset variable yields an empty string in both cases.
		inline std::string EnvironmentValue (const char * name)
		{
#ifdef _MSC_VER
			char * value = nullptr;
			std::size_t size = 0;
			if (_dupenv_s (&value, &size, name) != 0 || !value) return { };
			std::string result (value);
			std::free (value);
			return result;
#else
			const char * value = std::getenv (name);
			return value ? std::string (value) : std::string ();
#endif
		}

		inline bool DetectLightBackground ()
		{
			const std::string value = EnvironmentValue ("COLORFGBG");
			if (value.empty ()) return false;

			int last = -1;
			const char * cursor = value.c_str ();
			while (*cursor)
			{
				while (*cursor && (*cursor < '0' || *cursor > '9')) ++cursor;
				if (!*cursor) break;
				int number = 0;
				while (*cursor >= '0' && *cursor <= '9')
				{
					if (number < 1000000) number = number * 10 + (*cursor - '0');
					++cursor;
				}
				last = number;
			}
			if (last < 0 || last > 255) return false;

			int r = 0, g = 0, b = 0;
			if (!PaletteRgb (last, r, g, b)) return false;
			const int luminance = (r * 299 + g * 587 + b * 114) / 1000;
			return luminance >= 140;
		}
	}

	class Theme
	{
	public:
		/*
		 * Discover capabilities and allocate one colour pair per semantic
		 * role.
		 *
		 * The degradation ladder is re-entered after every failed allocation
		 * pass: direct colour -> xterm-256 -> 16-colour ANSI -> 8-colour ANSI
		 * -> monochrome attributes.  A terminal that cannot provide colours
		 * is not an error; the console keeps working with reverse/bold and
		 * the caller can report the degraded mode.
		 *
		 * Default terminal colours are used only when the background is
		 * known (COLORFGBG is set).  Without that hint an explicit black or
		 * white background is chosen from the detected luminance, because
		 * inheriting the terminal default can render dark text on a light
		 * background or the reverse.
		 *
		 * @post Every role maps either to an allocated pair or to a
		 *       monochrome attribute; Attributes() is always safe to call.
		 * @return True when at least one colour pair was installed.
		 */
		bool Init ()
		{
			m_RolePair.fill (0);
			m_Colors = 0;
			m_PairCount = 0;
			m_DefaultColors = false;
			m_BackgroundKnown = false;
			m_LightBackground = theme_detail::DetectLightBackground ();
			const std::string colorfgbg = theme_detail::EnvironmentValue ("COLORFGBG");
			m_BackgroundKnown = !colorfgbg.empty ();
			m_Mode = Mode::Monochrome;

			if (!terminal::HasColors ()) return false;
			if (!terminal::StartColor ()) return false;

			m_Colors = terminal::ColorCount ();
			m_PairCount = terminal::ColorPairCount ();
			// Default colours are safe only when the terminal background is
			// known. Otherwise use explicit pairs so dark text is never drawn
			// on a light background (or vice versa).
			m_DefaultColors = terminal::UseDefaultColors () && m_BackgroundKnown;
			if (m_Colors < 8) return false;

			m_Mode = SelectMode ();
			for (;;)
			{
				const std::size_t configured = InitPairs ();
				if (configured > 0 || m_Mode == Mode::Monochrome) break;
				switch (m_Mode)
				{
				case Mode::TrueColor: m_Mode = Mode::Ansi8; break;
				case Mode::Palette256: m_Mode = m_Colors >= 16 ? Mode::Ansi16 : Mode::Ansi8; break;
				case Mode::Ansi16: m_Mode = Mode::Ansi8; break;
				case Mode::Ansi8: m_Mode = Mode::Monochrome; break;
				case Mode::Monochrome: break;
				}
				if (m_Mode == Mode::Monochrome) break;
			}
			return m_Mode != Mode::Monochrome;
		}

		int Attributes (ThemeRole role) const
		{
			const std::size_t index = static_cast<std::size_t> (role);
			if (index >= RoleCount) return terminal::ATTR_NORMAL;
			if (m_Mode == Mode::Monochrome || m_RolePair[index] == 0) return theme_detail::MonoAttributes (role);
			return terminal::ColorPair (m_RolePair[index]) | theme_detail::Emphasis (role);
		}

		void Apply (ThemeRole role) const { terminal::SetAttribute (Attributes (role)); }

		bool HasColors () const { return m_Mode != Mode::Monochrome; }
		int Colors () const { return m_Colors; }
		int ColorPairs () const { return m_PairCount; }
		bool UsesDefaultColors () const { return m_DefaultColors; }
		bool UsesTrueColor () const { return m_Mode == Mode::TrueColor; }
		bool LightBackground () const { return m_LightBackground; }

		const char * CapabilityName () const
		{
			switch (m_Mode)
			{
			case Mode::TrueColor: return "direct-color";
			case Mode::Palette256: return "xterm-256";
			case Mode::Ansi16: return "ansi-16";
			case Mode::Ansi8: return "ansi-8";
			case Mode::Monochrome: return "monochrome";
			}
			return "unknown";
		}

		static const char * RoleName (ThemeRole role)
		{
			switch (role)
			{
			case ThemeRole::HeaderBar: return "header-bar";
			case ThemeRole::Title: return "title";
			case ThemeRole::StatusBar: return "status-bar";
			case ThemeRole::SelectedRow: return "selected-row";
			case ThemeRole::SelectedTab: return "selected-tab";
			case ThemeRole::PanelBorder: return "panel-border";
			case ThemeRole::Primary: return "primary";
			case ThemeRole::Secondary: return "secondary";
			case ThemeRole::Success: return "success";
			case ThemeRole::Warning: return "warning";
			case ThemeRole::Error: return "error";
			case ThemeRole::Disabled: return "disabled";
			case ThemeRole::Muted: return "muted";
			case ThemeRole::TorrentProgress: return "torrent-progress";
			case ThemeRole::TorrentDownload: return "torrent-download";
			case ThemeRole::TorrentUpload: return "torrent-upload";
			case ThemeRole::LogDebug: return "log-debug";
			case ThemeRole::LogInfo: return "log-info";
			case ThemeRole::LogWarning: return "log-warning";
			case ThemeRole::LogError: return "log-error";
			case ThemeRole::LogCritical: return "log-critical";
			case ThemeRole::Count: break;
			}
			return "unknown";
		}

	private:
		enum class Mode
		{
			Monochrome = 0,
			Ansi8,
			Ansi16,
			Palette256,
			TrueColor
		};

		static constexpr std::size_t RoleCount = static_cast<std::size_t> (ThemeRole::Count);

		Mode SelectMode () const
		{
			if (m_Colors >= 0x1000000)
			{
				// xterm-direct style terminfo uses RGB-packed colour
				// indices.  Direct colour is passed through
				// InitExtendedPair(); we do not redefine palette slots in
				// the user's terminal palette.
				return Mode::TrueColor;
			}
			if (m_Colors >= 256) return Mode::Palette256;
			if (m_Colors >= 16) return Mode::Ansi16;
			return Mode::Ansi8;
		}

		bool ResolveColors (ThemeRole role, int & foreground, int & background) const
		{
			if (m_Mode == Mode::TrueColor)
			{
				const auto spec = theme_detail::SpecFor (role, m_LightBackground);
				foreground = (spec.defaultForeground && m_DefaultColors) ? -1 : theme_detail::PackRgb (spec.foreground);
				background = (spec.defaultBackground && m_DefaultColors) ? -1 : theme_detail::PackRgb (spec.background);
				return true;
			}

			if (m_Mode == Mode::Palette256)
			{
				const auto spec = theme_detail::SpecFor (role, m_LightBackground);
				foreground
					= (spec.defaultForeground && m_DefaultColors) ? -1 : theme_detail::RgbTo256 (spec.foreground);
				background
					= (spec.defaultBackground && m_DefaultColors) ? -1 : theme_detail::RgbTo256 (spec.background);
				return true;
			}

			const auto spec = theme_detail::AnsiFor (role, m_LightBackground, m_Mode == Mode::Ansi16);
			foreground = (spec.defaultForeground && m_DefaultColors) ? -1 : spec.foreground;
			background = (spec.defaultBackground && m_DefaultColors) ? -1 : spec.background;
			if (spec.defaultForeground && !m_DefaultColors)
				foreground = m_LightBackground ? terminal::COLOR_BLACK : terminal::COLOR_WHITE;
			if (spec.defaultBackground && !m_DefaultColors)
				background = m_LightBackground ? terminal::COLOR_WHITE : terminal::COLOR_BLACK;
			return true;
		}

		std::size_t InitPairs ()
		{
			m_RolePair.fill (0);
			if (m_Mode == Mode::Monochrome) return 0;

			int pair = 1;
			std::size_t configured = 0;
			for (std::size_t index = 0; index < RoleCount; ++index)
			{
				const auto role = static_cast<ThemeRole> (index);
				if (role == ThemeRole::LogInfo) continue;
				if (pair >= m_PairCount || pair > 63) break;

				int foreground = -1;
				int background = -1;
				if (!ResolveColors (role, foreground, background)) continue;

				bool ok = false;
				if (m_Mode == Mode::TrueColor)
					ok = terminal::InitExtendedPair (pair, foreground, background);
				else
					ok = terminal::InitPair (pair, foreground, background);
				if (!ok) continue;

				m_RolePair[index] = static_cast<short> (pair);
				++pair;
				++configured;
			}
			return configured;
		}

		Mode m_Mode = Mode::Monochrome;
		int m_Colors = 0;
		int m_PairCount = 0;
		bool m_DefaultColors = false;
		bool m_BackgroundKnown = false;
		bool m_LightBackground = false;
		std::array<short, RoleCount> m_RolePair { };
	};
}
}

#endif // OPENI2PD_TUI_THEME_H
