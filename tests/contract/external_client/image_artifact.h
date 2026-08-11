#ifndef ULTRARENDER_IMAGE_ARTIFACT_H
#define ULTRARENDER_IMAGE_ARTIFACT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ure_image_evidence_t {
    uint64_t pixel_count;
    double minimum_rgb;
    double maximum_rgb;
    double mean_rgb;
} ure_image_evidence_t;

int ure_write_pfm_rgba(const char *path, const uint8_t *rgba,
                       uint32_t width, uint32_t height, uint64_t row_stride,
                       ure_image_evidence_t *evidence);

#ifdef __cplusplus
}
#endif

#endif
