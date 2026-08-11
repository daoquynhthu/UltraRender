#include "image_artifact.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

int ure_write_pfm_rgba(const char *path, const uint8_t *rgba,
                       uint32_t width, uint32_t height, uint64_t row_stride,
                       ure_image_evidence_t *evidence) {
    FILE *file = NULL;
    uint64_t count = 0;
    double minimum = DBL_MAX;
    double maximum = -DBL_MAX;
    double sum = 0.0;
    uint32_t y = 0;
    if (!path || !rgba || !evidence || width == 0 || height == 0 ||
        row_stride < (uint64_t)width * 16)
        return 0;
    for (y = 0; y < height; ++y) {
        const float *row = (const float *)(const void *)(rgba + row_stride * y);
        uint32_t x = 0;
        for (x = 0; x < width; ++x) {
            uint32_t component = 0;
            for (component = 0; component < 3; ++component) {
                const double value = row[x * 4 + component];
                if (!isfinite(value))
                    return 0;
                if (value < minimum)
                    minimum = value;
                if (value > maximum)
                    maximum = value;
                sum += value;
                ++count;
            }
        }
    }
    if (count == 0 || maximum <= 0.0 || maximum - minimum <= 1.0e-8)
        return 0;
    if (fopen_s(&file, path, "wb") != 0 || !file)
        return 0;
    if (fprintf(file, "PF\n%u %u\n-1.0\n", width, height) < 0) {
        fclose(file);
        return 0;
    }
    for (y = height; y != 0; --y) {
        const float *row =
            (const float *)(const void *)(rgba + row_stride * (y - 1));
        uint32_t x = 0;
        for (x = 0; x < width; ++x) {
            if (fwrite(row + x * 4, sizeof(float), 3, file) != 3) {
                fclose(file);
                return 0;
            }
        }
    }
    if (fclose(file) != 0)
        return 0;
    evidence->pixel_count = (uint64_t)width * height;
    evidence->minimum_rgb = minimum;
    evidence->maximum_rgb = maximum;
    evidence->mean_rgb = sum / (double)count;
    return 1;
}
