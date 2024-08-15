#include <iostream>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <limits>


struct WAVEHeader
{
    // [Master RIFF chunk]
    char fileTypeBlocID[4]; // (4 bytes) : Identifier « RIFF »  (0x52, 0x49, 0x46, 0x46)
    uint32_t fileSize;      // (4 bytes) : Overall file size minus 8 bytes
    char fileFormatID[4];   // (4 bytes) : Format = « WAVE »  (0x57, 0x41, 0x56, 0x45)
};


struct WAVEFormatChunk
{
    uint16_t audioFormat;  //    (2 bytes) : Audio format (1: PCM integer, 3: IEEE 754 float)
    uint16_t nbrChannels;  //    (2 bytes) : Number of channels
    uint32_t frequence;    //    (4 bytes) : Sample rate (in hertz)
    uint32_t bytePerSec;   //    (4 bytes) : Number of bytes to read per second (Frequence * BytePerBloc).
    uint16_t bytePerBloc;  //    (2 bytes) : Number of bytes per block (NbrChannels * BitsPerSample / 8).
    uint16_t bitsPerSample;//    (2 bytes) : Number of bits per sample
};



void read_wave(const char* filename, std::vector<float>& samples)
{
    FILE* f = fopen(filename, "rb");

    if (!f) {
        std::stringstream ss;
        ss << "Could not open " << filename << " for reading";
        throw std::runtime_error(ss.str());
    }

    WAVEHeader header;
    WAVEFormatChunk format;

    fread(&header, sizeof(WAVEHeader), 1, f);

    // std::cout << header.fileTypeBlocID[0] << header.fileTypeBlocID[1] << header.fileTypeBlocID[2] << header.fileTypeBlocID[3] << std::endl;
    // std::cout << header.fileSize << std::endl;
    // std::cout << header.fileFormatID[0] << header.fileFormatID[1] << header.fileFormatID[2] << header.fileFormatID[3] << std::endl;

    bool hasData = false;
    bool hasFormat = false;

    while (feof(f) == 0) {
        char formatBlocID[5] = {0};
        uint32_t blocSize;

        fread(formatBlocID, sizeof(char), 4, f);
        fread(&blocSize, sizeof(uint32_t), 1, f);

        if (strcmp(formatBlocID, "fmt ") == 0) {
            std::cout << "\"fmt \" of size: " << blocSize << std::endl;
            fread(&format, sizeof(WAVEFormatChunk), 1, f);
            hasFormat = true;
        } else if (strcmp(formatBlocID, "data") == 0) {
            std::cout << "\"data\" of size: " << blocSize << std::endl;
            samples.resize(blocSize / sizeof(float));
            fread(samples.data(), sizeof(float), samples.size(), f);
            hasData = true;
        } else {
            fseek(f, blocSize, SEEK_CUR);
        }

        if (hasData && hasFormat) {
            break;
        }
    }

    fclose(f);

    if (!hasData || !hasFormat) {
        throw std::runtime_error("Missing \"data\" or \"fmt \" chunk");
    }
}


void write_bw_pfm(const char* filepath, const std::vector<float> framebuffer, int width, int height)
{
    assert(framebuffer.size() == width * height);

    FILE* f = fopen(filepath, "wb");

    char type[2] = {'P', 'f'}; // Monochrome image
    char eol = 0x0a;
    char endianness[4] = {'-', '1', '.', '0'};

    fwrite(type, sizeof(char), 2, f);
    fwrite(&eol, sizeof(char), 1, f);

    fprintf(f, "%d %d", width, height);
    fwrite(&eol, sizeof(char), 1, f);

    fwrite(endianness, sizeof(char), 4, f);
    fwrite(&eol, sizeof(char), 1, f);

    fwrite(framebuffer.data(), sizeof(float), framebuffer.size(), f);
}


int main(int argc, char* argv[])
{
    if (argc <= 2) {
        std::cout << "Usage" << std::endl;
        std::cout << "-----" << std::endl;
        std::cout << argv[0] << " <wav file> <output pfm>" << std::endl;
        return 0;
    }
    
    const char* input_file  = argv[1];
    const char* output_file = argv[2];
    

    std::vector<float> wav_data;

    const int width  = 512;
    const int height = 100 * 640;
    const int acc_px = 7;

    std::vector<float> framebuffer(height * width);

    read_wave(input_file, wav_data);

    // Decode the signal
    int curr_x   = 0;
    int curr_y   = 0;
    int curr_acc = 0;

    bool write_ready = false;

    float prev_sample = 0;

    for (size_t i = 0; i < wav_data.size() / 2; i++) {
        const float sound_sample = wav_data[2 * i + 1];

        if (write_ready) {
            if (sound_sample >= 0.18f) {
                write_ready = false;
            } else if (curr_x < width && curr_y < height) {
                framebuffer[curr_y * width + curr_x] += sound_sample;
                ++curr_acc;

                if (curr_acc == acc_px) {
                    ++curr_x;
                    curr_acc = 0;
                }
            }
        } else {
            // Skip samples until the signal goes under a threshold
            if (sound_sample <= 0 && prev_sample < sound_sample) {
                ++curr_y;
                curr_x      = 0;
                curr_acc    = 0;
                write_ready = true;
            }
        }

        prev_sample = sound_sample;
    }

    std::cout << "read " << curr_y << " scanlines" << std::endl;

    // Rescale pixel values
    float min_val = std::numeric_limits<float>::max();
    float max_val = std::numeric_limits<float>::min();

    for (float& pixel: framebuffer) {
        // pixel /= (float)acc_px;
        min_val = std::min(min_val, pixel);
        max_val = std::max(max_val, pixel);
    }

    for (size_t i = 0; i < framebuffer.size(); i++) {
        // framebuffer[i] /= (float)acc_px;
        framebuffer[i] = (framebuffer[i] - min_val) / (max_val - min_val);
        framebuffer[i] = 1.f - std::max(0.f, std::min(1.f, framebuffer[i]));
    }

    write_bw_pfm(output_file, framebuffer, width, height);

    return 0;
}