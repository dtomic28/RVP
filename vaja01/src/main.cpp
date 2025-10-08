#include <iostream>
#include <string>
#include <cstdlib>
#include <random>
#include "bin_io.h"

using namespace binio;

#define DIFF 0b00
#define REPEAT 0b01
#define ABSVAL 0b10
#define END 0b11

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

    for (int i = 0; i< input.size(); ++i)
        std::cout << input[i] << "\n";

    BinWriter writer(outFileName);

    std::size_t b = 0;

    //Write first number
    writer.writeBytes(data.data(), 1); b+=8;

    std::size_t c = 1;
    while (c < input.size())
    {
        if (std::abs(input[c]) < 30 && input[c] != 0)
        {
            auto diff = encodeDifference(input[c]);
            writer.writeBits(DIFF, 2); b += 2;
            writer.writeBits(diff.prefix, 2); b+=2;
            writer.writeBits(diff.codedValue, diff.prefix + 2); b+= diff.prefix + 2;
            c++;
        }
        else if (std::abs(input[c]) > 30)
        {
            writer.writeBits(ABSVAL, 2); b += 2;
            writer.writeBits(input[c] > 0 ? 0 : 1, 1); b+=1;
            writer.writeBits(static_cast<uint8_t>(std::abs(input[c])), 8); b+=8;
            c++;
        }
        else
        {
            std::size_t lastZeroPos = c;
            while (lastZeroPos < input.size() && input[lastZeroPos] == 0 && (lastZeroPos - c) < 8)
                lastZeroPos++;

            std::size_t zeroCount =(lastZeroPos - c);
            writer.writeBits(REPEAT, 2); b += 2;
            writer.writeBits(zeroCount - 1ull, 3); b+=3;
            c += zeroCount;
        }
    }

    writer.writeBits(END, 2); b+=2;
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

        //std::vector<uint8_t>  input = generateRandomNumbers(N, M);
        std::vector<uint8_t> input = {55,53,53,53,53, 53,53,53,53, 53,10,10,11,11,11,11};

        compress(input, outputFile);

    } else if (mode == "d") {
        // ./myApp d input_file_name
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " d input_file_name\n";
            return 1;
        }

        std::string inputFile = argv[2];

        std::cout << "Decompressing file '" << inputFile << "'\n";

    } else {
        std::cerr << "Error: unknown mode '" << mode << "'. Expected 'c' or 'd'.\n";
        return 1;
    }

    return 0;
}
