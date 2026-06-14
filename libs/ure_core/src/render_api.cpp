#include "ure/render.hpp"

namespace ure {

int aov_channel_count(AovType type) {
    switch (type) {
    case AovType::Beauty:
    case AovType::Normal:
    case AovType::Albedo:
        return 3;
    case AovType::Depth:
        return 1;
    case AovType::Uv:
    case AovType::MotionVector:
        return 2;
    }
    return 0;
}

} // namespace ure
