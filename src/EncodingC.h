#pragma once

#include <cstddef>
#include <cstdint>

namespace bms_parser::detail {

// The small subset of encoding_c 0.9.8's C ABI used by this parser. Keeping
// opaque declarations here lets the amalgamation link the Rust static library
// without requiring encoding_c's generated headers. Versions are Cargo-locked.
struct Encoding;
struct Decoder;
extern "C" {
extern const Encoding *const SHIFT_JIS_ENCODING;
Decoder *encoding_new_decoder_without_bom_handling(const Encoding *encoding);
void encoding_new_decoder_without_bom_handling_into(const Encoding *encoding,
                                                   Decoder *decoder);
void decoder_free(Decoder *decoder);
size_t decoder_max_utf8_buffer_length(const Decoder *decoder, size_t byteLength);
uint32_t decoder_decode_to_utf8(Decoder *decoder, const uint8_t *source,
                               size_t *sourceLength, uint8_t *destination,
                               size_t *destinationLength, bool last,
                               bool *hadReplacements);
uint32_t decoder_decode_to_utf8_without_replacement(
    Decoder *decoder, const uint8_t *source, size_t *sourceLength,
    uint8_t *destination, size_t *destinationLength, bool last);
}

} // namespace bms_parser::detail
