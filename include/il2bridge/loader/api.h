#pragma once

// Internal symbols are hidden; public loader APIs opt into default visibility.
#if defined(__GNUC__) || defined(__clang__)
#define IL2BRIDGE_LOADER_API __attribute__((visibility("default")))
#else
#define IL2BRIDGE_LOADER_API
#endif
