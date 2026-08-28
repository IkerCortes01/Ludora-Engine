#pragma once
#include <cstdint>

namespace ludora {

using u8  = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

struct Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    Vec2() = default;
    Vec2(f32 px, f32 py) : x(px), y(py) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(f32 s)         const { return {x * s, y * s}; }

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
};

struct Size {
    i32 w = 0;
    i32 h = 0;
};

struct Recti {
    i32 x = 0, y = 0, w = 0, h = 0;
};

/// Color RGBA de 32 bits. El framebuffer es BGRA en memoria (formato nativo DIB),
/// la conversion la hace Color::packed().
struct Color {
    u8 r = 0, g = 0, b = 0, a = 255;

    constexpr Color() = default;
    constexpr Color(u8 pr, u8 pg, u8 pb, u8 pa = 255) : r(pr), g(pg), b(pb), a(pa) {}

    /// Empaqueta a 0xAARRGGBB, que en little-endian queda como BGRA en memoria.
    constexpr u32 packed() const {
        return (static_cast<u32>(a) << 24) | (static_cast<u32>(r) << 16) |
               (static_cast<u32>(g) << 8)  |  static_cast<u32>(b);
    }

    static constexpr Color fromHex(u32 rgb, u8 alpha = 255) {
        return Color{static_cast<u8>((rgb >> 16) & 0xFF),
                     static_cast<u8>((rgb >> 8)  & 0xFF),
                     static_cast<u8>( rgb        & 0xFF),
                     alpha};
    }
};

} // namespace ludora
