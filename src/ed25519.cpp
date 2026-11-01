// SPDX-License-Identifier: GPL-3.0-only
#include "ed25519.h"
#include <cstring>

namespace openkai2::crypto {
namespace {
constexpr uint64_t K[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull,
    0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull, 0x12835b0145706fbeull,
    0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull, 0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull,
    0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull, 0x983e5152ee66dfabull,
    0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
    0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull,
    0x53380d139d95b3dfull, 0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull,
    0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull, 0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull,
    0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull,
    0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull, 0xca273eceea26619cull,
    0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull,
    0x113f9804bef90daeull, 0x1b710b35131c471bull, 0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull,
    0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};

uint64_t rotr(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

void block(uint64_t state[8], const uint8_t *data) {
    uint64_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = 0;
        for (int j = 0; j < 8; ++j)
            w[i] = (w[i] << 8) | data[i * 8 + j];
    }
    for (int i = 16; i < 80; ++i) {
        const uint64_t s0 = rotr(w[i - 15], 1) ^ rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
        const uint64_t s1 = rotr(w[i - 2], 19) ^ rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6],
             h = state[7];
    for (int i = 0; i < 80; ++i) {
        const uint64_t t1 = h + (rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        const uint64_t t2 = (rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

Digest512 sha512_parts(std::initializer_list<std::pair<const uint8_t *, size_t>> parts) {
    uint64_t state[8] = {0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
                         0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
    uint8_t buffer[128];
    size_t used = 0;
    uint64_t total = 0;
    for (const auto &[data, size] : parts) {
        total += size;
        for (size_t i = 0; i < size; ++i) {
            buffer[used++] = data[i];
            if (used == 128) {
                block(state, buffer);
                used = 0;
            }
        }
    }
    buffer[used++] = 0x80;
    if (used > 112) {
        std::memset(buffer + used, 0, 128 - used);
        block(state, buffer);
        used = 0;
    }
    std::memset(buffer + used, 0, 128 - used);
    const uint64_t bits = total * 8;
    for (int i = 0; i < 8; ++i)
        buffer[127 - i] = uint8_t(bits >> (8 * i));
    block(state, buffer);
    Digest512 out{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            out[size_t(i * 8 + j)] = uint8_t(state[i] >> (56 - 8 * j));
    return out;
}

using gf = int64_t[16];
const gf gf0 = {0};
const gf gf1 = {1};
const gf D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
              0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
const gf D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
               0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
const gf X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
              0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
const gf Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
              0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
const gf I = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
              0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
const int64_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                       0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                       0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

void set(gf r, const gf a) {
    for (int i = 0; i < 16; ++i)
        r[i] = a[i];
}

void carry(gf o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += int64_t(1) << 16;
        const int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * (int64_t(1) << 16);
    }
}

void select(gf p, gf q, int b) {
    const int64_t c = ~(int64_t(b) - 1);
    for (int i = 0; i < 16; ++i) {
        const int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void pack_field(uint8_t *o, const gf n) {
    gf m, t;
    set(t, n);
    carry(t);
    carry(t);
    carry(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = int((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        select(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) {
        o[2 * i] = uint8_t(t[i] & 0xff);
        o[2 * i + 1] = uint8_t(t[i] >> 8);
    }
}

bool not_equal(const gf a, const gf b) {
    uint8_t c[32], d[32];
    pack_field(c, a);
    pack_field(d, b);
    uint8_t diff = 0;
    for (int i = 0; i < 32; ++i)
        diff |= uint8_t(c[i] ^ d[i]);
    return diff != 0;
}

uint8_t parity(const gf a) {
    uint8_t d[32];
    pack_field(d, a);
    return d[0] & 1;
}

void unpack_field(gf o, const uint8_t *n) {
    for (int i = 0; i < 16; ++i)
        o[i] = n[2 * i] + (int64_t(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

void add_f(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; ++i)
        o[i] = a[i] + b[i];
}

void sub_f(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; ++i)
        o[i] = a[i] - b[i];
}

void mul_f(gf o, const gf a, const gf b) {
    int64_t t[31] = {0};
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i)
        t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i)
        o[i] = t[i];
    carry(o);
    carry(o);
}

void square_f(gf o, const gf a) { mul_f(o, a, a); }

void invert(gf o, const gf i) {
    gf c;
    set(c, i);
    for (int a = 253; a >= 0; --a) {
        square_f(c, c);
        if (a != 2 && a != 4)
            mul_f(c, c, i);
    }
    set(o, c);
}

void pow2523(gf o, const gf i) {
    gf c;
    set(c, i);
    for (int a = 250; a >= 0; --a) {
        square_f(c, c);
        if (a != 1)
            mul_f(c, c, i);
    }
    set(o, c);
}

void add_point(gf p[4], gf q[4]) {
    gf a, b, c, d, t, e, f, g, h;
    sub_f(a, p[1], p[0]);
    sub_f(t, q[1], q[0]);
    mul_f(a, a, t);
    add_f(b, p[0], p[1]);
    add_f(t, q[0], q[1]);
    mul_f(b, b, t);
    mul_f(c, p[3], q[3]);
    mul_f(c, c, D2);
    mul_f(d, p[2], q[2]);
    add_f(d, d, d);
    sub_f(e, b, a);
    sub_f(f, d, c);
    add_f(g, d, c);
    add_f(h, b, a);
    mul_f(p[0], e, f);
    mul_f(p[1], h, g);
    mul_f(p[2], g, f);
    mul_f(p[3], e, h);
}

void swap_point(gf p[4], gf q[4], uint8_t b) {
    for (int i = 0; i < 4; ++i)
        select(p[i], q[i], b);
}

void pack_point(uint8_t *r, gf p[4]) {
    gf tx, ty, zi;
    invert(zi, p[2]);
    mul_f(tx, p[0], zi);
    mul_f(ty, p[1], zi);
    pack_field(r, ty);
    r[31] ^= uint8_t(parity(tx) << 7);
}

void scalar_multiply(gf p[4], gf q[4], const uint8_t *s) {
    set(p[0], gf0);
    set(p[1], gf1);
    set(p[2], gf1);
    set(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        const uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        swap_point(p, q, b);
        add_point(q, p);
        add_point(p, p);
        swap_point(p, q, b);
    }
}

void scalar_base(gf p[4], const uint8_t *s) {
    gf q[4];
    set(q[0], X);
    set(q[1], Y);
    set(q[2], gf1);
    mul_f(q[3], X, Y);
    scalar_multiply(p, q, s);
}

void mod_l(uint8_t *r, int64_t x[64]) {
    for (int i = 63; i >= 32; --i) {
        int64_t c = 0;
        int j;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += c - 16 * x[i] * L[j - (i - 32)];
            c = (x[j] + 128) >> 8;
            x[j] -= c * 256;
        }
        x[j] += c;
        x[i] = 0;
    }
    int64_t c = 0;
    for (int j = 0; j < 32; ++j) {
        x[j] += c - (x[31] >> 4) * L[j];
        c = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; ++j)
        x[j] -= c * L[j];
    for (int i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = uint8_t(x[i] & 255);
    }
}

void reduce(uint8_t *r) {
    int64_t x[64];
    for (int i = 0; i < 64; ++i)
        x[i] = r[i];
    for (int i = 0; i < 64; ++i)
        r[i] = 0;
    mod_l(r, x);
}

bool unpack_negative(gf r[4], const uint8_t *p) {
    gf t, chk, num, den, den2, den4, den6;
    set(r[2], gf1);
    unpack_field(r[1], p);
    square_f(num, r[1]);
    mul_f(den, num, D);
    sub_f(num, num, r[2]);
    add_f(den, r[2], den);
    square_f(den2, den);
    square_f(den4, den2);
    mul_f(den6, den4, den2);
    mul_f(t, den6, num);
    mul_f(t, t, den);
    pow2523(t, t);
    mul_f(t, t, num);
    mul_f(t, t, den);
    mul_f(t, t, den);
    mul_f(r[0], t, den);
    square_f(chk, r[0]);
    mul_f(chk, chk, den);
    if (not_equal(chk, num))
        mul_f(r[0], r[0], I);
    square_f(chk, r[0]);
    mul_f(chk, chk, den);
    if (not_equal(chk, num))
        return false;
    if (parity(r[0]) == (p[31] >> 7))
        sub_f(r[0], gf0, r[0]);
    mul_f(r[3], r[0], r[1]);
    return true;
}

bool scalar_canonical(const uint8_t *s) {
    for (int i = 31; i >= 0; --i) {
        if (s[i] < L[i])
            return true;
        if (s[i] > L[i])
            return false;
    }
    return false;
}
}

Digest512 sha512(const uint8_t *data, size_t size) { return sha512_parts({{data, size}}); }

PublicKey public_key(const Seed &seed) {
    auto d = sha512(seed.data(), seed.size());
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
    gf p[4];
    scalar_base(p, d.data());
    PublicKey key{};
    pack_point(key.data(), p);
    return key;
}

Signature sign(const Seed &seed, const uint8_t *message, size_t size) {
    const PublicKey key = public_key(seed);
    auto d = sha512(seed.data(), seed.size());
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
    auto r = sha512_parts({{d.data() + 32, 32}, {message, size}});
    reduce(r.data());
    gf p[4];
    scalar_base(p, r.data());
    Signature out{};
    pack_point(out.data(), p);
    auto h = sha512_parts({{out.data(), 32}, {key.data(), 32}, {message, size}});
    reduce(h.data());
    int64_t x[64] = {0};
    for (int i = 0; i < 32; ++i)
        x[i] = r[size_t(i)];
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 32; ++j)
            x[i + j] += int64_t(h[size_t(i)]) * d[size_t(j)];
    mod_l(out.data() + 32, x);
    return out;
}

bool verify(const PublicKey &key, const uint8_t *message, size_t size, const Signature &signature) {
    if (!scalar_canonical(signature.data() + 32))
        return false;
    gf q[4], p[4];
    if (!unpack_negative(q, key.data()))
        return false;
    auto h = sha512_parts({{signature.data(), 32}, {key.data(), 32}, {message, size}});
    reduce(h.data());
    scalar_multiply(p, q, h.data());
    scalar_base(q, signature.data() + 32);
    add_point(p, q);
    uint8_t t[32];
    pack_point(t, p);
    uint8_t diff = 0;
    for (int i = 0; i < 32; ++i)
        diff |= uint8_t(t[i] ^ signature[size_t(i)]);
    return diff == 0;
}

std::string to_hex(const uint8_t *data, size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 15]);
    }
    return out;
}

bool from_hex(const std::string &text, uint8_t *out, size_t size) {
    if (text.size() != size * 2)
        return false;
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < size; ++i) {
        const int high = value(text[2 * i]), low = value(text[2 * i + 1]);
        if (high < 0 || low < 0)
            return false;
        out[i] = uint8_t(high << 4 | low);
    }
    return true;
}

}
