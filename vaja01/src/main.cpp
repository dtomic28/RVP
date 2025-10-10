#include <iostream>
#include <string>
#include <cstdlib>
#include <random>
#include <cassert>
#include "bin_io.h"
#include <filesystem>

using namespace binio;

#define DIFF 0b00
#define REPEAT 0b01
#define ABSVAL 0b10
#define END 0b11

//#define DEBUG_PRINT

// --- Debug print helper ---
#ifdef DEBUG_PRINT
#define DEBUG_LOG(x) do { std::cout << x << std::endl; } while(0)
    #define DEBUG_LOGF(fmt, ...) do { \
        printf(fmt "\n", __VA_ARGS__); \
        fflush(stdout); \
    } while(0)
#else
#define DEBUG_LOG(x) do {} while(0)
#define DEBUG_LOGF(fmt, ...) do {} while(0)
#endif


struct EncodedDiff
{
    uint8_t prefix;
    uint8_t codedValue;
};

EncodedDiff encodeDifference(int value) {
    EncodedDiff r{0,0};

    if (value >= -2 && value <= 2 && value != 0) {
        r.prefix = 0; // "00" -> 2 bits
        // [-2,-1] -> 0..1 ; [ +1,+2 ] -> 2..3
        r.codedValue = (value < 0) ? static_cast<uint8_t>(value + 2)
                                   : static_cast<uint8_t>(value + 1);
    }
    else if ((value >= -6 && value <= -3) || (value >= 3 && value <= 6)) {
        r.prefix = 1; // "01" -> 3 bits
        // [-6..-3] -> 0..3 ; [ +3..+6 ] -> 4..7
        r.codedValue = (value < 0) ? static_cast<uint8_t>(value + 6)
                                   : static_cast<uint8_t>(value + 1);
    }
    else if ((value >= -14 && value <= -7) || (value >= 7 && value <= 14)) {
        r.prefix = 2; // "10" -> 4 bits
        // [-14..-7] -> 0..7 ; [ +7..+14 ] -> 8..15
        r.codedValue = (value < 0) ? static_cast<uint8_t>(value + 14)
                                   : static_cast<uint8_t>(value + 1);
    }
    else if ((value >= -30 && value <= -15) || (value >= 15 && value <= 30)) {
        r.prefix = 3; // "11" -> 5 bits
        // [-30..-15] -> 0..15 ; [ +15..+30 ] -> 16..31
        r.codedValue = (value < 0) ? static_cast<uint8_t>(value + 30)
                                   : static_cast<uint8_t>(value + 1);
    }
    else {
        throw std::out_of_range("Value out of supported encoding range.");
    }

    return r;
}

int decodeDifference(const EncodedDiff& d)
{
    int value = 0;

    switch (d.prefix)
    {
        case 0: // "00" -> range [-2..-1, +1..+2]
            if (d.codedValue <= 1)
                value = static_cast<int>(d.codedValue) - 2;   // 0→-2, 1→-1
            else
                value = static_cast<int>(d.codedValue) - 1;   // 2→+1, 3→+2
            break;

        case 1: // "01" -> range [-6..-3, +3..+6]
            if (d.codedValue <= 3)
                value = static_cast<int>(d.codedValue) - 6;   // 0→-6 .. 3→-3
            else
                value = static_cast<int>(d.codedValue) - 1;   // 4→+3 .. 7→+6
            break;

        case 2: // "10" -> range [-14..-7, +7..+14]
            if (d.codedValue <= 7)
                value = static_cast<int>(d.codedValue) - 14;  // 0→-14 .. 7→-7
            else
                value = static_cast<int>(d.codedValue) - 1;   // 8→+7 .. 15→+14
            break;

        case 3: // "11" -> range [-30..-15, +15..+30]
            if (d.codedValue <= 15)
                value = static_cast<int>(d.codedValue) - 30;  // 0→-30 .. 15→-15
            else
                value = static_cast<int>(d.codedValue) - 1;   // 16→+15 .. 31→+30
            break;

        default:
            throw std::out_of_range("Invalid prefix in EncodedDiff.");
    }

    return value;
}

uint8_t randomUint8(uint8_t min, uint8_t max) {
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<int> dist(min, max);
    return static_cast<uint8_t>(dist(gen));
}

std::vector<uint8_t> generateRandomNumbers(std::size_t count, uint8_t maxDifference)
{
    std::vector<uint8_t> results;

    results.push_back(randomUint8(0, 255));

    while (results.size() < count)
    {
        int last = static_cast<int>(results.back());
        int lower = std::max(0, last - static_cast<int>(maxDifference));
        int upper = std::min(255, last + static_cast<int>(maxDifference));

        results.push_back(randomUint8(static_cast<uint8_t>(lower),
                                      static_cast<uint8_t>(upper)));

    }

    return results;
}

void compress(std::vector<uint8_t> data, std::string outFileName)
{
    std::vector<int> input(data.size());

    //Calculate the diff.
    input[0] = data[0];
    for (size_t i = 1; i < data.size(); ++i)
        input[i] = data[i] - data[i - 1];

    BinWriter writer(outFileName);

    //Write first number
    writer.writeBytes(data.data(), 1);

    std::size_t c = 1;
    while (c < input.size())
    {
        if (std::abs(input[c]) <= 30 && input[c] != 0)
        {
            DEBUG_LOG("DIFF");
            auto diff = encodeDifference(input[c]);
            writer.writeBits(DIFF, 2);;
            writer.writeBits(diff.prefix, 2);
            writer.writeBits(diff.codedValue, diff.prefix + 2);
            c++;
        }
        else if (std::abs(input[c]) > 30)
        {
            DEBUG_LOG("ABSVAL");
            writer.writeBits(ABSVAL, 2);
            writer.writeBits(input[c] > 0 ? 0 : 1, 1);
            writer.writeBits(static_cast<uint8_t>(std::abs(input[c])), 8);
            c++;
        }
        else
        {
            DEBUG_LOG("REPEAT");
            std::size_t lastZeroPos = c;
            while (lastZeroPos < input.size() && input[lastZeroPos] == 0 && (lastZeroPos - c) < 8)
                lastZeroPos++;

            assert(lastZeroPos > 0);

            std::size_t zeroCount =(lastZeroPos - c);
            writer.writeBits(REPEAT, 2);
            writer.writeBits(zeroCount - 1ull, 3);
            c += zeroCount;
        }
    }

    DEBUG_LOG("END");
    writer.writeBits(END, 2);
}

std::vector<uint8_t> decompress(std::string inFileName)
{
    std::vector<int> readValues;

    BinReader reader(inFileName);
    if (!reader.good())
        return {};

    uint64_t initialValue;

    reader.readBits(initialValue, 8);

    readValues.push_back(static_cast<int>(initialValue));

    uint64_t instructionBits;
    do
    {
        reader.readBits(instructionBits, 2);

        switch(instructionBits)
        {
            case DIFF:
            {
                DEBUG_LOG("DIFF");
                uint64_t diffPrefix;
                uint64_t diffValue;
                reader.readBits(diffPrefix, 2);

                reader.readBits(diffValue, static_cast<uint8_t>(diffPrefix) + 2);

                assert(diffPrefix <= 0b11);

                int val = decodeDifference({static_cast<uint8_t>(diffPrefix), static_cast<uint8_t>(diffValue)});
                readValues.push_back(val);
                break;
            }
            case REPEAT:
            {
                DEBUG_LOG("REPEAT");
                uint64_t numOfZeros;

                reader.readBits(numOfZeros, 3);
                numOfZeros++;

                for(int i = 0; i<numOfZeros; i++)
                    readValues.push_back(0);
                break;
            }
            case ABSVAL:
            {
                DEBUG_LOG("ABSVAL");
                bool signedBit;
                reader.readBit(signedBit);
                int multiplier = signedBit ? -1 : 1;

                uint64_t absCodedNum;
                reader.readBits(absCodedNum, 8);

                readValues.push_back(static_cast<int>(absCodedNum) * multiplier);
                break;
            }
            default:
                break;
        }
    } while(instructionBits != END);

    std::vector<uint8_t> result(readValues.size());
    result[0] = static_cast<uint8_t>(readValues[0]);

    for (size_t i = 1; i < readValues.size(); ++i) {
        int prev = static_cast<int>(result[i - 1]);
        int val  = prev + readValues[i];
        result[i] = val;
    }

    return result;
}

bool testDataIntegrity(bool keep_files = false) {
    const std::string in_raw  = "integrity_input.bin";   // optional (raw input)
    const std::string out_cmp = "integrity_test.bin";    // compressed file

    std::vector<uint8_t> original = generateRandomNumbers(5000, 30);

    // (Optional) save the original raw bytes for debugging
    {
        std::ofstream raw(in_raw, std::ios::binary);
        if (!raw) {
            std::cerr << "ERROR: cannot create " << in_raw << "\n";
            return false;
        }
        raw.write(reinterpret_cast<const char*>(original.data()),
                  static_cast<std::streamsize>(original.size()));
    }

    // 2) Compress -> file
    compress(original, out_cmp);

    if (!std::filesystem::exists(out_cmp) ||
        std::filesystem::file_size(out_cmp) == 0) {
        std::cerr << "ERROR: compressed file missing or empty: " << out_cmp << "\n";
        return false;
    }

    // 3) Decompress <- file
    std::vector<uint8_t> reconstructed = decompress(out_cmp);

    // 4) Compare
    if (reconstructed.size() != original.size()) {
        std::cerr << "BIG ERROR: size mismatch! original=" << original.size()
                  << " reconstructed=" << reconstructed.size() << "\n";
        return false;
    }

    for (std::size_t i = 0; i < original.size(); ++i) {
        if (reconstructed[i] != original[i]) {
            std::cerr << "BIG ERROR: data mismatch at index " << i
                      << " (orig=" << static_cast<int>(original[i])
                      << ", rec="  << static_cast<int>(reconstructed[i]) << ")\n";
            return false;
        }
    }

    std::cout << "Integrity OK: reconstructed data matches original ("
              << original.size() << " bytes)\n";

    if (!keep_files) {
        std::error_code ec;
        std::filesystem::remove(in_raw, ec);
        std::filesystem::remove(out_cmp, ec);
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " c N M [output_file_name]\n"
                  << "  " << argv[0] << " d input_file_name\n";
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "c") {
        // ./myApp c N M [output_file_name]
        if (argc < 4 || argc > 5) {
            std::cerr << "Usage: " << argv[0] << " c N M [output_file_name]\n";
            return 1;
        }

        int N = std::atoi(argv[2]);
        int M = std::atoi(argv[3]);
        std::string outputFile = (argc == 5) ? argv[4] : "out.bin";

        if (N <= 0 || M <= 0) {
            std::cerr << "Error: N and M must be positive integers.\n";
            return 1;
        }

        std::cout << "Compressing with N=" << N
                  << ", M=" << M
                  << ", output='" << outputFile << "'\n";

        std::vector<uint8_t>  input = generateRandomNumbers(N, M);
        compress(input, outputFile);

    } else if (mode == "d") {
        // ./myApp d input_file_name
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " d input_file_name\n";
            return 1;
        }

        std::string inputFile = argv[2];
        std::cout << "Decompressing file '" << inputFile << "'\n";

        auto decompressed = decompress(inputFile);

    }else if (mode == "t") {
        bool ok = testDataIntegrity(/*keep_files=*/false);
        return ok ? 0 : 2;
    }
    else {
        std::cerr << "Error: unknown mode '" << mode << "'. Expected 'c' or 'd'.\n";
        return 1;
    }

    return 0;
}
