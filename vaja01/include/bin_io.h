#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <ostream>
#include <istream>
#include <fstream>
#include <memory>
#include <array>
#include <limits>

namespace binio {

enum class Endianness { Little, Big, Native
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
    = (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ? Little : Big)
#else
    // conservative default on unknown compilers
    = Little
#endif
};

namespace detail {

// constrain to integral (including char types)
template <typename T>
using enable_integral_t = std::enable_if_t<std::is_integral<T>::value, int>;

template <typename T, enable_integral_t<T> = 0>
inline std::array<std::uint8_t, sizeof(T)> to_bytes(T value, Endianness e) {
    using U = std::make_unsigned_t<T>;
    U u = static_cast<U>(value); // reinterpret signed as unsigned of same width
    std::array<std::uint8_t, sizeof(T)> b{};
    if (e == Endianness::Native) {
        std::memcpy(b.data(), &u, sizeof(T));
        return b;
    }
    if (e == Endianness::Little) {
        for (std::size_t i = 0; i < sizeof(T); ++i)
            b[i] = static_cast<std::uint8_t>((u >> (8 * i)) & 0xFFu);
    } else { // Big
        for (std::size_t i = 0; i < sizeof(T); ++i)
            b[sizeof(T) - 1 - i] = static_cast<std::uint8_t>((u >> (8 * i)) & 0xFFu);
    }
    return b;
}

template <typename T, enable_integral_t<T> = 0>
inline T from_bytes(const std::uint8_t* b, Endianness e) {
    using U = std::make_unsigned_t<T>;
    U u = 0;
    if (e == Endianness::Native) {
        std::memcpy(&u, b, sizeof(T));
    } else if (e == Endianness::Little) {
        for (std::size_t i = 0; i < sizeof(T); ++i)
            u |= (static_cast<U>(b[i]) << (8 * i));
    } else { // Big
        for (std::size_t i = 0; i < sizeof(T); ++i)
            u |= (static_cast<U>(b[sizeof(T) - 1 - i]) << (8 * i));
    }
    return static_cast<T>(u);
}

} // namespace detail

class BinWriter {
public:
    // Construct from filename (owns stream)
    explicit BinWriter(const std::string& filename)
        : owned_stream_(std::make_unique<std::ofstream>(filename, std::ios::binary)),
          out_(*owned_stream_), bit_buffer_(0), bit_count_(0) {}

    // Construct from an existing ostream (does not own)
    explicit BinWriter(std::ostream& out)
        : out_(out), bit_buffer_(0), bit_count_(0) {}

    // no copying
    BinWriter(const BinWriter&) = delete;
    BinWriter& operator=(const BinWriter&) = delete;

    // flushing in destructor keeps header-only usage frictionless
    ~BinWriter() { flush(); }

    // Write a single bit; MSB-first within each produced byte
    inline void writeBit(bool bit) {
        bit_buffer_ <<= 1;
        if (bit) bit_buffer_ |= 0x01u;
        ++bit_count_;
        if (bit_count_ == 8) flushByte_();
    }

    // Write up to 64 bits from value, MSB-first
    inline void writeBits(std::uint64_t value, int numBits) {
        // guard
        if (numBits < 0 || numBits > 64) return;
        for (int i = numBits - 1; i >= 0; --i)
            writeBit( (value >> i) & 1u );
    }

    // Write a full integral type (byte-aligned). Pads current byte if needed.
    template <typename T, detail::enable_integral_t<T> = 0>
    inline void write(T value, Endianness e = Endianness::Native) {
        alignToByte(); // ensure aligned
        auto bytes = detail::to_bytes<T>(value, e);
        out_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // Write raw bytes (byte-aligned)
    inline void writeBytes(const void* data, std::size_t n) {
        alignToByte();
        out_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
    }

    // Align to next byte boundary, padding remaining bits with `pad` (0 or 1)
    inline void alignToByte(std::uint8_t pad = 0) {
        if (bit_count_ == 0) return;
        if (pad) {
            while (bit_count_ != 0) writeBit(true);
        } else {
            // pad zeros at once
            bit_buffer_ <<= (8 - bit_count_);
            flushByte_();
        }
    }

    // Explicit flush of any partial byte and underlying stream
    inline void flush() {
        if (bit_count_ != 0) {
            bit_buffer_ <<= (8 - bit_count_);
            flushByte_();
        }
        out_.flush();
    }

    // Check stream state
    inline bool good() const { return out_.good(); }

private:
    inline void flushByte_() {
        out_.put(static_cast<char>(bit_buffer_));
        bit_buffer_ = 0;
        bit_count_ = 0;
    }

    std::unique_ptr<std::ostream> owned_base_; // unused but reserved
    std::unique_ptr<std::ofstream> owned_stream_; // present only if constructed from filename
    std::ostream& out_;
    std::uint8_t bit_buffer_;
    int bit_count_;
};

class BinReader {
public:
    // Construct from filename (owns stream)
    explicit BinReader(const std::string& filename)
        : owned_stream_(std::make_unique<std::ifstream>(filename, std::ios::binary)),
          in_(*owned_stream_), bit_buffer_(0), bit_count_(0), eof_(false) {}

    // Construct from existing istream (does not own)
    explicit BinReader(std::istream& in)
        : in_(in), bit_buffer_(0), bit_count_(0), eof_(false) {}

    // no copying
    BinReader(const BinReader&) = delete;
    BinReader& operator=(const BinReader&) = delete;

    // Read a single bit into `bit`. Returns false on EOF/underflow.
    inline bool readBit(bool& bit) {
        if (bit_count_ == 0) {
            int c = in_.get();
            if (c == EOF) { eof_ = true; return false; }
            bit_buffer_ = static_cast<std::uint8_t>(c);
            bit_count_ = 8;
        }
        bit = (bit_buffer_ & 0x80u) != 0;
        bit_buffer_ <<= 1;
        --bit_count_;
        return true;
    }

    // Read up to 64 bits into value; returns false on EOF/underflow
    inline bool readBits(std::uint64_t& value, int numBits) {
        if (numBits < 0 || numBits > 64) return false;
        value = 0;
        for (int i = 0; i < numBits; ++i) {
            bool b;
            if (!readBit(b)) return false;
            value = (value << 1) | static_cast<std::uint64_t>(b);
        }
        return true;
    }

    // Read a full integral type (byte-aligned). Returns false on failure.
    template <typename T, detail::enable_integral_t<T> = 0>
    inline bool read(T& outValue, Endianness e = Endianness::Native) {
        alignToByte();
        std::array<std::uint8_t, sizeof(T)> b{};
        if (!in_.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size())))
            { eof_ = true; return false; }
        outValue = detail::from_bytes<T>(b.data(), e);
        return true;
    }

    // Read raw bytes (byte-aligned). Returns number of bytes actually read.
    inline std::size_t readBytes(void* dst, std::size_t n) {
        alignToByte();
        in_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
        auto got = static_cast<std::size_t>(in_.gcount());
        if (got < n) eof_ = true;
        return got;
    }

    // Discard leftover bits in current byte
    inline void alignToByte() {
        bit_buffer_ = 0;
        bit_count_ = 0;
    }

    inline bool good() const { return in_.good(); }
    inline bool eof()  const { return eof_ || in_.eof(); }

private:
    std::unique_ptr<std::ifstream> owned_stream_; // present only if constructed from filename
    std::istream& in_;
    std::uint8_t bit_buffer_;
    int bit_count_;
    bool eof_;
};

} // namespace binio
