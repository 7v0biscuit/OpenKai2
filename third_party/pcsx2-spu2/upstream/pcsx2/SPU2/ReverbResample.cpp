// SPDX-FileCopyrightText: 2002-2024 PCSX2 Dev Team
// SPDX-License-Identifier: LGPL-3.0+

#include "SPU2/defs.h"

MULTI_ISA_UNSHARED_START

static constexpr u32 NUM_TAPS = 39;
// 39 tap filter, the 0's could be optimized out
static constexpr std::array<s16, 48> filter_down_coefs alignas(32) = {
	-1,
	0,
	2,
	0,
	-10,
	0,
	35,
	0,
	-103,
	0,
	266,
	0,
	-616,
	0,
	1332,
	0,
	-2960,
	0,
	10246,
	16384,
	10246,
	0,
	-2960,
	0,
	1332,
	0,
	-616,
	0,
	266,
	0,
	-103,
	0,
	35,
	0,
	-10,
	0,
	2,
	0,
	-1,
};

static constexpr std::array<s16, 48> make_up_coefs()
{
	std::array<s16, 48> ret = {};

	for (u32 i = 0; i < NUM_TAPS; i++)
	{
		ret[i] = static_cast<s16>(std::clamp<s32>(filter_down_coefs[i] * 2, INT16_MIN, INT16_MAX));
	}

	return ret;
}

static constexpr std::array<s16, 48> filter_up_coefs alignas(32) = make_up_coefs();

s32 __forceinline ReverbDownsample_reference(V_Core& core, bool right)
{
	int index = (core.RevbSampleBufPos - NUM_TAPS) & 63;
	s32 out = 0;

	for (u32 i = 0; i < NUM_TAPS; i++)
	{
		out += core.RevbDownBuf[right][index + i] * filter_down_coefs[i];
	}

	out >>= 15;

	return clamp_mix(out);
}

s32 ReverbDownsample(V_Core& core, bool right)
{
	return ReverbDownsample_reference(core, right);
}

StereoOut32 __forceinline ReverbUpsample_reference(V_Core& core)
{
	int index = (core.RevbSampleBufPos - NUM_TAPS) & 63;
	s32 l = 0, r = 0;

	for (u32 i = 0; i < NUM_TAPS; i++)
	{
		l += core.RevbUpBuf[0][index + i] * filter_up_coefs[i];
		r += core.RevbUpBuf[1][index + i] * filter_up_coefs[i];
	}

	l >>= 15;
	r >>= 15;

	return {clamp_mix(l), clamp_mix(r)};
}

StereoOut32 ReverbUpsample(V_Core& core)
{
	return ReverbUpsample_reference(core);
}

MULTI_ISA_UNSHARED_END
