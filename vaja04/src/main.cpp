#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cassert>

#include "bin_io.h"

using namespace binio;

//#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define DEBUG_LOG(x) do { std::cout << x << std::endl; } while(0)
#else
#define DEBUG_LOG(x) do {} while(0)
#endif

#pragma pack(push, 1)
struct BmpFileHeader {
    uint16_t bfType;      // 'BM'
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};

struct BmpInfoHeader {
    uint32_t biSize;          // 40
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;      // 8 for grayscale
    uint32_t biCompression;   // 0 = BI_RGB
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)

static bool loadBmpGray8(const std::string& fileName, std::vector<uint8_t>& pixels, int& height, int& width)
{
    std::ifstream in(fileName, std::ios::binary);
    if (!in) {
        std::cerr << "ERROR: cannot open BMP: " << fileName << "\n";
        return false;
    }

    BmpFileHeader fh{};
    BmpInfoHeader ih{};

    in.read(reinterpret_cast<char*>(&fh), sizeof(fh));
    in.read(reinterpret_cast<char*>(&ih), sizeof(ih));

    if (!in) {
        std::cerr << "ERROR: failed reading BMP headers\n";
        return false;
    }

    if (fh.bfType != 0x4D42) {
        std::cerr << "ERROR: not a BMP file\n";
        return false;
    }
    if (ih.biBitCount != 8 || ih.biCompression != 0 || ih.biPlanes != 1) {
        std::cerr << "ERROR: expected 8-bit uncompressed BMP (grayscale palette)\n";
        return false;
    }

    width  = ih.biWidth;
    height = std::abs(ih.biHeight);
    if (width <= 0 || height <= 0) {
        std::cerr << "ERROR: invalid BMP dimensions\n";
        return false;
    }

    // Seek to pixel array
    in.seekg(static_cast<std::streamoff>(fh.bfOffBits), std::ios::beg);
    if (!in) {
        std::cerr << "ERROR: invalid BMP bfOffBits\n";
        return false;
    }

    // BMP rows are padded to 4 bytes
    int rowSize = ((width + 3) / 4) * 4;
    std::vector<uint8_t> row((size_t)rowSize);

    pixels.assign((size_t)width * (size_t)height, 0);

    bool bottomUp = (ih.biHeight > 0);

    for (int y = 0; y < height; ++y) {
        in.read(reinterpret_cast<char*>(row.data()), rowSize);
        if (!in) {
            std::cerr << "ERROR: failed reading BMP pixel data\n";
            return false;
        }

        int dstY = bottomUp ? (height - 1 - y) : y;
        std::memcpy(&pixels[(size_t)dstY * (size_t)width], row.data(), (size_t)width);
    }

    return true;
}

static bool saveBmpGray8(const std::string& fileName, const std::vector<uint8_t>& pixels, int height, int width)
{
    if (height <= 0 || width <= 0) return false;
    if (pixels.size() != (size_t)height * (size_t)width) return false;

    std::ofstream out(fileName, std::ios::binary);
    if (!out) {
        std::cerr << "ERROR: cannot create BMP: " << fileName << "\n";
        return false;
    }

    int rowSize = ((width + 3) / 4) * 4;
    uint32_t paletteSize = 256u * 4u;
    uint32_t pixelDataSize = (uint32_t)(rowSize * height);

    BmpFileHeader fh{};
    BmpInfoHeader ih{};

    fh.bfType = 0x4D42;
    fh.bfOffBits = (uint32_t)(sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + paletteSize);
    fh.bfSize = fh.bfOffBits + pixelDataSize;

    ih.biSize = sizeof(BmpInfoHeader);
    ih.biWidth = width;
    ih.biHeight = height; // bottom-up
    ih.biPlanes = 1;
    ih.biBitCount = 8;
    ih.biCompression = 0;
    ih.biSizeImage = pixelDataSize;
    ih.biClrUsed = 256;
    ih.biClrImportant = 256;

    out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
    out.write(reinterpret_cast<const char*>(&ih), sizeof(ih));

    // grayscale palette
    for (int i = 0; i < 256; ++i) {
        uint8_t entry[4] = { (uint8_t)i, (uint8_t)i, (uint8_t)i, 0 };
        out.write(reinterpret_cast<const char*>(entry), 4);
    }

    std::vector<uint8_t> row((size_t)rowSize, 0);

    // bottom-up write
    for (int y = height - 1; y >= 0; --y) {
        std::memcpy(row.data(), &pixels[(size_t)y * (size_t)width], (size_t)width);
        out.write(reinterpret_cast<const char*>(row.data()), row.size());
    }

    return out.good();
}


// MED predictor (JPEG-LS)
static uint8_t medPredict(uint8_t a, uint8_t b, uint8_t c)
{
    int ai = (int)a, bi = (int)b, ci = (int)c;
    int mx = std::max(ai, bi);
    int mn = std::min(ai, bi);

    int p;
    if (ci >= mx) p = mn;
    else if (ci <= mn) p = mx;
    else p = ai + bi - ci;

    if (p < 0) p = 0;
    if (p > 255) p = 255;
    return (uint8_t)p;
}

// ceil(log2(v)) for v>=1
static int ceilLog2_u32(uint32_t v)
{
    if (v <= 1) return 0;
    int bits = 0;
    uint32_t x = v - 1;
    while (x) { x >>= 1; ++bits; }
    return bits;
}

// Predict errors E (E = Pred - P), first element and borders per slides
static void predict(const std::vector<uint8_t>& P, int X, int Y, std::vector<int32_t>& E)
{
    E.assign((size_t)X * (size_t)Y, 0);

    auto at = [&](int x, int y) -> uint8_t {
        return P[(size_t)y * (size_t)Y + (size_t)x];
    };

    for (int y = 0; y < X; ++y) {
        for (int x = 0; x < Y; ++x) {
            int i = y * Y + x;

            if (x == 0 && y == 0) {
                E[(size_t)i] = (int32_t)at(0, 0);
            } else if (y == 0) {
                E[(size_t)i] = (int32_t)at(x - 1, 0) - (int32_t)at(x, 0);
            } else if (x == 0) {
                E[(size_t)i] = (int32_t)at(0, y - 1) - (int32_t)at(0, y);
            } else {
                uint8_t a = at(x - 1, y);
                uint8_t b = at(x, y - 1);
                uint8_t c = at(x - 1, y - 1);
                uint8_t pred = medPredict(a, b, c);
                E[(size_t)i] = (int32_t)pred - (int32_t)at(x, y);
            }
        }
    }
}

// Inverse prediction: P = Pred - E, borders per slides
static void predictInverse(const std::vector<int32_t>& E, int X, int Y, std::vector<uint8_t>& P)
{
    P.assign((size_t)X * (size_t)Y, 0);

    auto idx = [&](int x, int y) -> size_t {
        return (size_t)y * (size_t)Y + (size_t)x;
    };

    for (int y = 0; y < X; ++y) {
        for (int x = 0; x < Y; ++x) {
            int i = y * Y + x;

            if (x == 0 && y == 0) {
                int v = (int)E[(size_t)i];
                v = std::clamp(v, 0, 255);
                P[idx(0, 0)] = (uint8_t)v;
            } else if (y == 0) {
                int v = (int)P[idx(x - 1, 0)] - (int)E[(size_t)i];
                v = std::clamp(v, 0, 255);
                P[idx(x, 0)] = (uint8_t)v;
            } else if (x == 0) {
                int v = (int)P[idx(0, y - 1)] - (int)E[(size_t)i];
                v = std::clamp(v, 0, 255);
                P[idx(0, y)] = (uint8_t)v;
            } else {
                uint8_t a = P[idx(x - 1, y)];
                uint8_t b = P[idx(x, y - 1)];
                uint8_t c = P[idx(x - 1, y - 1)];
                uint8_t pred = medPredict(a, b, c);

                int v = (int)pred - (int)E[(size_t)i];
                v = std::clamp(v, 0, 255);
                P[idx(x, y)] = (uint8_t)v;
            }
        }
    }
}

// Interpolative coding (IC)
static void icEncode(BinWriter& w, const std::vector<uint32_t>& C, int L, int H)
{
    if (H - L <= 1) return;

    uint32_t cL = C[(size_t)L];
    uint32_t cH = C[(size_t)H];

    if (cH == cL) return;

    int m = (L + H) / 2;
    uint32_t range = (cH - cL) + 1u;
    int g = ceilLog2_u32(range);

    uint32_t v = C[(size_t)m] - cL;

    w.writeBits(v, g);

    if (L < m) icEncode(w, C, L, m);
    if (m < H) icEncode(w, C, m, H);
}

static bool icDecode(BinReader& r, std::vector<uint32_t>& C, int L, int H)
{
    if (H - L <= 1) return true;

    uint32_t cL = C[(size_t)L];
    uint32_t cH = C[(size_t)H];

    if (cL == cH) {
        for (int i = L + 1; i <= H - 1; ++i)
            C[(size_t)i] = cL;
        return true;
    }

    int m = (L + H) / 2;
    uint32_t range = (cH - cL) + 1u;
    int g = ceilLog2_u32(range);

    uint64_t bits = 0;
    if (!r.readBits(bits, g)) return false;

    C[(size_t)m] = cL + (uint32_t)bits;

    if (L < m) { if (!icDecode(r, C, L, m)) return false; }
    if (m < H) { if (!icDecode(r, C, m, H)) return false; }

    return true;
}

static void compressFlocic(const std::vector<uint8_t>& P, int X, int Y, const std::string& outFileName)
{
    uint32_t n = (uint32_t)((size_t)X * (size_t)Y);
    assert(P.size() == (size_t)n);

    std::vector<int32_t> E;
    predict(P, X, Y, E);

    // Map E -> N (non-negative)
    std::vector<uint32_t> N((size_t)n, 0);
    N[0] = (uint32_t)E[0]; // first is pixel 0..255
    for (uint32_t i = 1; i < n; ++i) {
        int32_t e = E[(size_t)i];
        if (e >= 0) N[(size_t)i] = (uint32_t)(2u * (uint32_t)e);
        else        N[(size_t)i] = (uint32_t)(2u * (uint32_t)(-e) - 1u);
    }

    // Cumulative C
    std::vector<uint32_t> C((size_t)n, 0);
    C[0] = N[0];
    for (uint32_t i = 1; i < n; ++i)
        C[(size_t)i] = C[(size_t)(i - 1)] + N[(size_t)i];

    // Header: X(16), C0(8), Cn-1(32), n(32)
    BinWriter w(outFileName);
    w.write<uint16_t>((uint16_t)X, Endianness::Little);
    w.write<uint8_t>((uint8_t)C[0], Endianness::Little);
    w.write<uint32_t>(C[(size_t)(n - 1)], Endianness::Little);
    w.write<uint32_t>(n, Endianness::Little);

    if (n >= 2)
        icEncode(w, C, 0, (int)n - 1);

    w.flush();
}

static bool decompressFlocic(const std::string& inFileName, std::vector<uint8_t>& outP, int& outX, int& outY)
{
    BinReader r(inFileName);
    if (!r.good()) {
        std::cerr << "ERROR: cannot open input: " << inFileName << "\n";
        return false;
    }

    uint16_t X = 0;
    uint8_t c0 = 0;
    uint32_t cLast = 0;
    uint32_t n = 0;

    if (!r.read<uint16_t>(X, Endianness::Little)) return false;
    if (!r.read<uint8_t>(c0, Endianness::Little)) return false;
    if (!r.read<uint32_t>(cLast, Endianness::Little)) return false;
    if (!r.read<uint32_t>(n, Endianness::Little)) return false;

    if (X == 0 || n == 0 || (n % X) != 0) {
        std::cerr << "ERROR: invalid header\n";
        return false;
    }

    uint32_t Y = n / X;
    outX = (int)X;
    outY = (int)Y;

    std::vector<uint32_t> C((size_t)n, 0);
    C[0] = (uint32_t)c0;
    C[(size_t)(n - 1)] = cLast;

    if (n >= 2) {
        if (!icDecode(r, C, 0, (int)n - 1)) {
            std::cerr << "ERROR: IC decode failed\n";
            return false;
        }
    }

    // N from diffs of C
    std::vector<uint32_t> N((size_t)n, 0);
    N[0] = C[0];
    for (uint32_t i = 1; i < n; ++i)
        N[(size_t)i] = C[(size_t)i] - C[(size_t)(i - 1)];

    // E from N
    std::vector<int32_t> E((size_t)n, 0);
    E[0] = (int32_t)N[0];
    for (uint32_t i = 1; i < n; ++i) {
        uint32_t v = N[(size_t)i];
        if ((v & 1u) == 0) E[(size_t)i] = (int32_t)(v / 2u);
        else               E[(size_t)i] = -(int32_t)((v + 1u) / 2u);
    }

    predictInverse(E, outX, outY, outP);
    return true;
}


static std::string changeExtension(const std::string& path, const std::string& ext)
{
    std::filesystem::path p(path);
    p.replace_extension(ext);
    return p.string();
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " c input.bmp\n"
                  << "  " << argv[0] << " d input.flocic\n";
        return 1;
    }

    std::string mode = argv[1];
    std::string fileName = argv[2];

    if (mode == "c") {
        std::vector<uint8_t> P;
        int X = 0, Y = 0;

        if (!loadBmpGray8(fileName, P, X, Y))
            return 2;

        std::string outFile = changeExtension(fileName, ".flocic");
        std::cout << "Compressing '" << fileName << "' -> '" << outFile << "'\n";
        compressFlocic(P, X, Y, outFile);

        return 0;
    }

    if (mode == "d") {
        std::vector<uint8_t> P;
        int X = 0, Y = 0;

        if (!decompressFlocic(fileName, P, X, Y))
            return 2;

        std::string outBmp = changeExtension(fileName, "_dec.bmp");
        std::cout << "Decompressing '" << fileName << "' -> '" << outBmp << "'\n";

        if (!saveBmpGray8(outBmp, P, X, Y)) {
            std::cerr << "ERROR: failed to write bmp\n";
            return 3;
        }

        return 0;
    }

    std::cerr << "Error: unknown mode '" << mode << "'. Expected 'c' or 'd'.\n";
    return 1;
}
