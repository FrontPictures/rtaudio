#pragma once
#include <math.h>

inline float generate_sin(int x, int samplerate, float frequency, float amplitude)
{
    const float t = 2.0 * 3.14 * frequency / samplerate;
    return amplitude * sinf(t * x);
}

template<class T>
void write_float_to_data(T** data_out, bool planar, int channels, float value, int sample,
    bool mix = false, int dst_offset = 0)
{
    T val = 0;
    if (std::is_integral_v<T>) {
        int bits_count = sizeof(T) * 8;
        uint64_t values_count = pow(2, bits_count);
        if (std::is_signed_v<T>) {
            uint64_t half_values_count = values_count / 2;
            value *= half_values_count;
        }
        else if (std::is_unsigned_v<T>) {
            value += 1;
            value *= values_count;
        }
        val = value;
    }
    else if (std::is_floating_point_v<T>) {
        val = value;
    }
    for (int c = 0; c < channels; c++) {
        if (planar) {
            if (!mix)
                data_out[c][sample + dst_offset] = val;
            else
                data_out[c][sample + dst_offset] += val;
        }
        else {
            if (!mix)
                data_out[0][channels * (sample + dst_offset) + c] = val;
            else
                data_out[0][channels * (sample + dst_offset) + c] += val;
        }
    }
}

template<class T>
void fill_sin(T** data_out_void, bool planar, int channels, int samplerate, int samples_count,
    int offset = 0, int frequency = 500, float amplitude = 0.5, bool mix = false,
    int dst_offset = 0)
{
    float y = 0;
    for (int x = 0; x < samples_count; x++) {
        y = generate_sin(x + offset, samplerate, frequency, amplitude);
        write_float_to_data(data_out_void, planar, channels, y, x, mix, dst_offset);
    }
}
