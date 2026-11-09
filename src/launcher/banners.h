// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>

namespace openkai2::launcher {
struct BannerPicture {
    const unsigned char *data;
    std::size_t size;
};
extern const BannerPicture banner_pictures[];
}
