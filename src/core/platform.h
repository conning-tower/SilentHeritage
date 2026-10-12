// The platform the program is built for (not in the original).
#pragma once

namespace sh {

// Android: always fullscreen, a finger for the mouse.
#ifdef __ANDROID__
inline constexpr bool kAndroid = true;
#else
inline constexpr bool kAndroid = false;
#endif

}  // namespace sh
