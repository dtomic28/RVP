// dct_rle.cpp
// C++17 + OpenCV 8x8 DCT/RLE codec using user's binio utilities.
//
// Header: width (uint16), height (uint16), little-endian.
// Channels: encoded R, then G, then B (contiguous).
// Factor 0..15: triangular zeroing mask; 15 zeros everything incl. DC.
// DC: 12-bit two's complement; AC: rules A/B/C w/ variable 1..13 bits.

#include <opencv2/opencv.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "bin_io.h"

namespace codec {

namespace detail {

// zig-zag for 8x8
static const int kZigZag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

// triangular mask values 1..15 (zero when value <= factor; factor==15 zeroes all incl. DC)
static const int kTriMask[8][8] = {
    {15,14,13,12,11,10, 9, 8},
    {14,13,12,11,10, 9, 8, 7},
    {13,12,11,10, 9, 8, 7, 6},
    {12,11,10, 9, 8, 7, 6, 5},
    {11,10, 9, 8, 7, 6, 5, 4},
    {10, 9, 8, 7, 6, 5, 4, 3},
    { 9, 8, 7, 6, 5, 4, 3, 2},
    { 8, 7, 6, 5, 4, 3, 2, 1}
};

template <typename T>
inline T clamp(T v, T lo, T hi) {
    return std::max(lo, std::min(hi, v));
}

inline cv::Mat padToMultiple(const cv::Mat& src, int block) {
    const int nh = ((src.rows + block - 1) / block) * block;
    const int nw = ((src.cols + block - 1) / block) * block;
    if (nh == src.rows && nw == src.cols) return src;
    cv::Mat out;
    cv::copyMakeBorder(src, out, 0, nh - src.rows, 0, nw - src.cols, cv::BORDER_REPLICATE);
    return out;
}

inline void applyTriangularZeroing(cv::Mat& b8x8, int factor) {
    if (factor <= 0) return;
    // factor==15 -> zero all incl. DC.
    for (int u = 0; u < 8; ++u) {
        float* row = b8x8.ptr<float>(u);
        for (int v = 0; v < 8; ++v) {
            const int mark = kTriMask[u][v];
            if (factor >= 15 || (mark >= 1 && factor >= mark)) row[v] = 0.0f;
        }
    }
}

inline uint64_t toTwos(int v, int bits) {
    const uint64_t mask = (bits == 64) ? ~0ull : ((1ull << bits) - 1ull);
    if (v < 0) {
        uint64_t pos = static_cast<uint64_t>(-v);
        return ((~pos) + 1ull) & mask;
    }
    return static_cast<uint64_t>(v) & mask;
}

inline int fromTwos(uint64_t u, int bits) {
    if (bits <= 0) return 0;
    const uint64_t sign = 1ull << (bits - 1);
    const uint64_t mask = (bits == 64) ? ~0ull : ((1ull << bits) - 1ull);
    u &= mask;
    if (u & sign) {
        int64_t s = static_cast<int64_t>(u | (~mask));
        return static_cast<int>(s);
    }
    return static_cast<int>(u);
}

inline int bitsForSigned(int v, int maxBits = 13) {
    for (int b = 1; b <= maxBits; ++b) {
        if (fromTwos(toTwos(v, b), b) == v) return b;
    }
    return maxBits;
}

inline std::array<int,64> blockToZigZag(const cv::Mat& b) {
    std::array<int,64> out{};
    for (int k = 0; k < 64; ++k) {
        const int u = kZigZag[k] / 8;
        const int v = kZigZag[k] % 8;
        out[k] = static_cast<int>(std::lrint(b.at<float>(u, v)));
    }
    return out;
}

inline cv::Mat zigZagToBlock(const std::array<int,64>& zz) {
    cv::Mat b(8, 8, CV_32F);
    for (int k = 0; k < 64; ++k) {
        const int u = kZigZag[k] / 8;
        const int v = kZigZag[k] % 8;
        b.at<float>(u, v) = static_cast<float>(zz[k]);
    }
    return b;
}

inline void encodeBlock(const std::array<int,64>& zz, binio::BinWriter& bw) {
    // DC (12 bits)
    int dc = clamp(zz[0], -2040, 2040);
    bw.writeBits(toTwos(dc, 12), 12);

    int i = 1;
    while (i < 64) {
        int run = 0;
        while (i + run < 64 && zz[i + run] == 0) ++run;

        if (i + run >= 64) {
            // Rule B: 0 + 6-bit run
            bw.writeBit(false);
            bw.writeBits(static_cast<uint64_t>(run), 6);
            i += run;
            break;
        }

        int ac = clamp(zz[i + run], -4080, 4080);
        int L = clamp(bitsForSigned(ac, 13), 1, 13);

        // Rule A: 0 + 6(run) + 4(L) + L(bits)
        bw.writeBit(false);
        bw.writeBits(static_cast<uint64_t>(run), 6);
        bw.writeBits(static_cast<uint64_t>(L), 4);
        bw.writeBits(toTwos(ac, L), L);

        i += run + 1;
    }
}

inline bool decodeBlock(binio::BinReader& br, std::array<int,64>& zz) {
    std::uint64_t u = 0;
    if (!br.readBits(u, 12)) return false;
    zz[0] = fromTwos(u, 12);

    int count = 1;
    while (count < 64) {
        bool flag = false;
        if (!br.readBit(flag)) return false;

        if (!flag) {
            std::uint64_t run = 0;
            if (!br.readBits(run, 6)) return false;

            if (count + static_cast<int>(run) >= 64) {
                for (int k = 0; k < static_cast<int>(run) && count < 64; ++k) zz[count++] = 0;
                break;
            }

            std::uint64_t L = 0;
            if (!br.readBits(L, 4)) return false;
            if (L == 0 || L > 13) return false;

            std::uint64_t val = 0;
            if (!br.readBits(val, static_cast<int>(L))) return false;

            for (int k = 0; k < static_cast<int>(run); ++k) zz[count++] = 0;
            zz[count++] = fromTwos(val, static_cast<int>(L));
        } else {
            std::uint64_t L = 0;
            if (!br.readBits(L, 4)) return false;
            if (L == 0 || L > 13) return false;

            std::uint64_t val = 0;
            if (!br.readBits(val, static_cast<int>(L))) return false;

            zz[count++] = fromTwos(val, static_cast<int>(L));
        }
    }
    return true;
}

inline void compressChannel(const cv::Mat& ch, binio::BinWriter& bw, int factor) {
    cv::Mat f32;
    ch.convertTo(f32, CV_32F);
    f32 -= 128.0f;

    const int H = f32.rows, W = f32.cols;
    for (int y = 0; y < H; y += 8) {
        for (int x = 0; x < W; x += 8) {
            cv::Mat blk = f32(cv::Rect(x, y, 8, 8)).clone();
            cv::dct(blk, blk);
            applyTriangularZeroing(blk, factor);
            const auto zz = blockToZigZag(blk);
            encodeBlock(zz, bw);
        }
    }
}

inline bool decompressChannel(binio::BinReader& br, int width, int height, cv::Mat& out) {
    out.create(height, width, CV_8U);
    cv::Mat f32(height, width, CV_32F);

    for (int y = 0; y < height; y += 8) {
        for (int x = 0; x < width; x += 8) {
            std::array<int,64> zz{};
            if (!decodeBlock(br, zz)) return false;
            cv::Mat blk = zigZagToBlock(zz);
            cv::idct(blk, blk);
            blk += 128.0f;

            for (int u = 0; u < 8; ++u) {
                for (int v = 0; v < 8; ++v) {
                    const int yy = y + u, xx = x + v;
                    int iv = clamp(static_cast<int>(std::lrint(blk.at<float>(u,v))), 0, 255);
                    out.at<std::uint8_t>(yy, xx) = static_cast<std::uint8_t>(iv);
                }
            }
        }
    }
    return true;
}

} // namespace detail

inline void compressImage(const std::string& inPath, const std::string& outPath, int factor) {
    if (factor < 0 || factor > 15) throw std::runtime_error("factor must be in [0..15]");

    cv::Mat bgr = cv::imread(inPath, cv::IMREAD_COLOR);
    if (bgr.empty()) throw std::runtime_error("failed to read input image");

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    rgb = detail::padToMultiple(rgb, 8);

    const int W = rgb.cols;
    const int H = rgb.rows;

    std::vector<cv::Mat> ch;
    cv::split(rgb, ch); // R,G,B

    binio::BinWriter bw(outPath);
    bw.write<std::uint16_t>(static_cast<std::uint16_t>(W), binio::Endianness::Little);
    bw.write<std::uint16_t>(static_cast<std::uint16_t>(H), binio::Endianness::Little);

    for (int c = 0; c < 3; ++c) detail::compressChannel(ch[c], bw, factor);
    bw.flush();
    if (!bw.good()) throw std::runtime_error("write failed");
}

inline void decompressImage(const std::string& inPath, const std::string& outPath) {
    binio::BinReader br(inPath);

    std::uint16_t W = 0, H = 0;
    if (!br.read<std::uint16_t>(W, binio::Endianness::Little)) throw std::runtime_error("header width read failed");
    if (!br.read<std::uint16_t>(H, binio::Endianness::Little)) throw std::runtime_error("header height read failed");

    cv::Mat r, g, b;
    if (!detail::decompressChannel(br, W, H, r)) throw std::runtime_error("decode R failed");
    if (!detail::decompressChannel(br, W, H, g)) throw std::runtime_error("decode G failed");
    if (!detail::decompressChannel(br, W, H, b)) throw std::runtime_error("decode B failed");

    std::vector<cv::Mat> ch = { r, g, b };
    cv::Mat rgb, bgr;
    cv::merge(ch, rgb);
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

    if (!cv::imwrite(outPath, bgr)) throw std::runtime_error("failed to write output image");
}

} // namespace codec

// CLI
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr
            << "DCT+RLE\n"
            << "Compress:   " << argv[0] << " c <input_image> <output_bin> <factor 0..15>\n"
            << "Decompress: " << argv[0] << " d <input_bin> <output_image>\n";
        return 1;
    }
    try {
        const char mode = argv[1][0];
        if (mode == 'c') {
            if (argc < 5) { std::cerr << "missing args\n"; return 2; }
            const std::string inImg = argv[2];
            const std::string outBin = argv[3];
            const int factor = std::stoi(argv[4]);
            codec::compressImage(inImg, outBin, factor);
            std::cout << "ok -> " << outBin << "\n";
        } else if (mode == 'd') {
            if (argc < 4) { std::cerr << "missing args\n"; return 2; }
            const std::string inBin = argv[2];
            const std::string outImg = argv[3];
            codec::decompressImage(inBin, outImg);
            std::cout << "ok -> " << outImg << "\n";
        } else {
            std::cerr << "unknown mode\n";
            return 3;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 10;
    }
    return 0;
}
