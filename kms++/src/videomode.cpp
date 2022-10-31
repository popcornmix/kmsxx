#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cmath>
#include <sstream>
#include <fmt/format.h>

#include <kms++/kms++.h>
#include "helpers.h"

using namespace std;

namespace kms
{
bool Videomode::valid() const
{
	return !!clock;
}

unique_ptr<Blob> Videomode::to_blob(Card& card) const
{
	drmModeModeInfo drm_mode = video_mode_to_drm_mode(*this);

	return unique_ptr<Blob>(new Blob(card, &drm_mode, sizeof(drm_mode)));
}

float Videomode::calculated_vrefresh() const
{
	// XXX interlace should only halve visible vertical lines, not blanking
	float refresh = (clock * 1000.0) / (htotal * vtotal) * (interlace() ? 2 : 1);
	return roundf(refresh * 100.0) / 100.0;
}

bool Videomode::interlace() const
{
	return flags & DRM_MODE_FLAG_INTERLACE;
}

SyncPolarity Videomode::hsync() const
{
	if (flags & DRM_MODE_FLAG_PHSYNC)
		return SyncPolarity::Positive;
	if (flags & DRM_MODE_FLAG_NHSYNC)
		return SyncPolarity::Negative;
	return SyncPolarity::Undefined;
}

SyncPolarity Videomode::vsync() const
{
	if (flags & DRM_MODE_FLAG_PVSYNC)
		return SyncPolarity::Positive;
	if (flags & DRM_MODE_FLAG_NVSYNC)
		return SyncPolarity::Negative;
	return SyncPolarity::Undefined;
}

void Videomode::set_interlace(bool ilace)
{
	if (ilace)
		flags |= DRM_MODE_FLAG_INTERLACE;
	else
		flags &= ~DRM_MODE_FLAG_INTERLACE;
}

void Videomode::set_hsync(SyncPolarity pol)
{
	flags &= ~(DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NHSYNC);

	switch (pol) {
	case SyncPolarity::Positive:
		flags |= DRM_MODE_FLAG_PHSYNC;
		break;
	case SyncPolarity::Negative:
		flags |= DRM_MODE_FLAG_NHSYNC;
		break;
	default:
		break;
	}
}

void Videomode::set_vsync(SyncPolarity pol)
{
	flags &= ~(DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_NVSYNC);

	switch (pol) {
	case SyncPolarity::Positive:
		flags |= DRM_MODE_FLAG_PVSYNC;
		break;
	case SyncPolarity::Negative:
		flags |= DRM_MODE_FLAG_NVSYNC;
		break;
	default:
		break;
	}
}

string Videomode::to_string_short() const
{
	return fmt::format("{}x{}{}@{:.2f}", hdisplay, vdisplay, interlace() ? "i" : "", calculated_vrefresh());
}

static char sync_to_char(SyncPolarity pol)
{
	switch (pol) {
	case SyncPolarity::Positive:
		return '+';
	case SyncPolarity::Negative:
		return '-';
	default:
		return '?';
	}
}

static const char *mode_type_names[] = {
	"builtin",
	"clock_c",
	"crtc_c",
	"PRF", //"preferred",
	"DEF", //"default",
	"USR", //"userdef",
	"DRV", //"driver",
};

static const char *mode_flag_names[] = {
	"|", //"phsync",
	"|", //"nhsync",
	"|", //"pvsync",
	"|", //"nvsync",
	"INT", //"interlace",
	"dblscan",
	"csync",
	"pcsync",
	"ncsync",
	"hskew",
	"bcast",
	"pixmux",
	"2x", //"dblclk",
	"clkdiv2",
	"[5],|,3dfp,3dfa,3dla,3dsbs,3dldepth,3dgfx,3dtab,3dsbs", NULL, NULL, NULL, NULL,
	"[4],|,4:3,16:9,64:27,256:135", NULL, NULL, NULL,
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

const string mode_str(uint32_t type, const char *names[], unsigned int size) {
	unsigned int i;
	string s;
	const char *sep = "";
	for (i = 0; i < size;) {
		string name;
		// this is an N bit field with lookup into given string
		int bits = 1;
		if (names[i] && names[i][0] == '[') {
			bits = atoi(names[i] + 1);
			unsigned int v = (type >> i) & ((1 << bits) - 1);
			char *tok = strdup(names[i] + 3);
			char *s = strtok(tok, ",");
			while (v-- > 0)
				s = strtok(NULL, ",");
			if (s)
				name = fmt::format("{}", s);
			// skip extra bits
			free(tok);
		} else {
			if (names[i]) {
				if (type & (1 << i))
					name = fmt::format("{}", names[i]);
				else
					name = "|";
			}
		}
		if (name.size() > 0 && name != "|") {
			s += fmt::format("{}{}", sep, name);
			sep = "|";
		}
		if (name.size() > 0)
			type &= ~(((1 << bits) - 1) << i);
		i += bits;
	}
	// if there are bits undecoded, just display the hex code
	if (type)
		return fmt::format("0x{:x}", type);
	return s;
}


string Videomode::to_string_long() const
{
	string h = fmt::format("{}/{}/{}/{}/{}", hdisplay, hfp(), hsw(), hbp(), sync_to_char(hsync()));
	string v = fmt::format("{}/{}/{}/{}/{}", vdisplay, vfp(), vsw(), vbp(), sync_to_char(vsync()));

	string str = fmt::format("{} {:.3f} {} {} {} ({:.2f}) {} {}",
				 to_string_short(),
				 clock / 1000.0,
				 h, v,
				 vrefresh, calculated_vrefresh(),
				 mode_str(type, mode_type_names, ARRAY_SIZE(mode_type_names)),
				 mode_str(flags, mode_flag_names, ARRAY_SIZE(mode_flag_names)));

	return str;
}

string Videomode::to_string_long_padded() const
{
	string h = fmt::format("{}/{}/{}/{}/{}", hdisplay, hfp(), hsw(), hbp(), sync_to_char(hsync()));
	string v = fmt::format("{}/{}/{}/{}/{}", vdisplay, vfp(), vsw(), vbp(), sync_to_char(vsync()));

	string str = fmt::format("{:<16} {:7.3f} {:<18} {:<18} {:2} ({:.2f}) {:<10} {}",
				 to_string_short(),
				 clock / 1000.0,
				 h, v,
				 vrefresh, calculated_vrefresh(),
				 mode_str(type, mode_type_names, ARRAY_SIZE(mode_type_names)),
				 mode_str(flags, mode_flag_names, ARRAY_SIZE(mode_flag_names)));

	return str;
}

Videomode videomode_from_timings(uint32_t clock_khz,
				 uint16_t hact, uint16_t hfp, uint16_t hsw, uint16_t hbp,
				 uint16_t vact, uint16_t vfp, uint16_t vsw, uint16_t vbp)
{
	Videomode m{};
	m.clock = clock_khz;

	m.hdisplay = hact;
	m.hsync_start = hact + hfp;
	m.hsync_end = hact + hfp + hsw;
	m.htotal = hact + hfp + hsw + hbp;

	m.vdisplay = vact;
	m.vsync_start = vact + vfp;
	m.vsync_end = vact + vfp + vsw;
	m.vtotal = vact + vfp + vsw + vbp;

	return m;
}

} // namespace kms
