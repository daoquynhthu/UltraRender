#pragma once

namespace ure {

inline constexpr int kMinSpectralPacketLanes = 1;
inline constexpr int kMaxSpectralPacketLanes = 32;

constexpr bool valid_spectral_packet_lane_count(int lanes) {
    return lanes == 1 ||
        (lanes >= 8 && lanes <= kMaxSpectralPacketLanes);
}

}
