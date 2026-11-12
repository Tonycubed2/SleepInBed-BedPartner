#pragma once

// Loaded first in every source file (CMake target_precompile_headers):
// the standard headers CommonLib expects, then CommonLib itself, so that
// SimpleIni.h / SKSEMenuFramework.h (which include windows.h) come after it.

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

using namespace std::literals;
