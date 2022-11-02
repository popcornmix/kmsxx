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

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

template<typename T>
std::string join(const T& values, const std::string& delim)
{
	std::ostringstream ss;
	for (const auto& v : values) {
		if (&v != &values[0])
			ss << delim;
		ss << v;
	}
	return ss.str();
}

static const char *mode_type_names[] = {
	// the first 3 are deprecated so don't care about a short name
	"builtin", // deprecated
	"clock_c", // deprecated
	"crtc_c", // deprecated
	"P", // "preferred",
	"default", // deprecated,
	"U", // "userdef",
	"D", // "driver",
};

static const char *mode_flag_names[] = {
	// the first 5 flags are displayed elsewhere
	NULL, // "phsync",
	NULL, // "nhsync",
	NULL, // "pvsync",
	NULL, // "nvsync",
	NULL, // "interlace",
	"dblscan",
	"csync",
	"pcsync",
	"ncsync",
	"hskew",
	"bcast", // deprecated
	"pixmux", // deprecated
	"2x", // "dblclk",
	"clkdiv2",
};

static const char *mode_3d_names[] = {
	NULL, "3dfp", "3dfa", "3dla", "3dsbs", "3dldepth", "3dgfx", "3dtab", "3dsbs",
};

static const char *mode_aspect_names[] = {
	NULL, "4:3", "16:9", "64:27", "256:135",
};

const string mode_type_str(uint32_t inval) {
	uint32_t val = inval;
	vector<string> v;
	for (size_t i = 0; i < ARRAY_SIZE(mode_type_names); i++) {
		if (val & (1 << i)) {
			v.push_back(mode_type_names[i]);
			val &= ~ (1 << i);
		}
	}
	if (val != 0)
		return fmt::format("0x{:x}", inval);
	return join(v, "|");
}

const string mode_flag_str(uint32_t inval) {
	uint32_t val = inval;
	vector<string> v;
	for (size_t i = 0; i < ARRAY_SIZE(mode_flag_names); i++) {
		if (val & (1 << i)) {
			if (mode_flag_names[i])
				v.push_back(mode_flag_names[i]);
			val &= ~(1 << i);
		}
	}
	uint32_t threed = (val >> 14) & 0x1f;
	if (threed < ARRAY_SIZE(mode_3d_names)) {
		if (mode_3d_names[threed])
			v.push_back(mode_3d_names[threed]);
		val &= ~(0x1f << 14);
	}
	uint32_t aspect = (val >> 19) & 0xf;
	if (aspect < ARRAY_SIZE(mode_aspect_names)) {
		if (mode_aspect_names[aspect])
			v.push_back(mode_aspect_names[aspect]);
		val &= ~(0xf << 19);
	}

	if (val != 0)
		return fmt::format("0x{:x}", inval);
	return join(v, "|");
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
				 mode_type_str(type),
				 mode_flag_str(flags));

	return str;
}

string Videomode::to_string_long_padded() const
{
	string h = fmt::format("{}/{}/{}/{}/{}", hdisplay, hfp(), hsw(), hbp(), sync_to_char(hsync()));
	string v = fmt::format("{}/{}/{}/{}/{}", vdisplay, vfp(), vsw(), vbp(), sync_to_char(vsync()));

	string str = fmt::format("{:<16} {:7.3f} {:<18} {:<18} {:2} ({:.2f}) {:<7} {}",
				 to_string_short(),
				 clock / 1000.0,
				 h, v,
				 vrefresh, calculated_vrefresh(),
				 mode_type_str(type),
				 mode_flag_str(flags));

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
