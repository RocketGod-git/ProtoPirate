#pragma once

#include "kia_generic.h"

#define KIA_PROTOCOL_V3_V4_NAME "Kia V3/V4"

extern const SubGhzProtocol kia_protocol_v3_v4;

void* kia_protocol_decoder_v3_v4_alloc(SubGhzEnvironment* environment);
void kia_protocol_decoder_v3_v4_free(void* context);
void kia_protocol_decoder_v3_v4_reset(void* context);
void kia_protocol_decoder_v3_v4_feed(void* context, bool level, uint32_t duration);
uint8_t kia_protocol_decoder_v3_v4_get_hash_data(void* context);
SubGhzProtocolStatus kia_protocol_decoder_v3_v4_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    kia_protocol_decoder_v3_v4_deserialize(void* context, FlipperFormat* flipper_format);
void kia_protocol_decoder_v3_v4_get_string(void* context, FuriString* output);
