#pragma once

#include "kia_generic.h"

#define KIA_PROTOCOL_V1_NAME "Kia V1"

typedef struct SubGhzProtocolDecoderKiaV1 SubGhzProtocolDecoderKiaV1;
typedef struct SubGhzProtocolEncoderKiaV1 SubGhzProtocolEncoderKiaV1;

extern const SubGhzProtocolDecoder kia_protocol_v1_decoder;
extern const SubGhzProtocolEncoder kia_protocol_v1_encoder;
extern const SubGhzProtocol kia_protocol_v1;

void* kia_protocol_decoder_v1_alloc(SubGhzEnvironment* environment);
void kia_protocol_decoder_v1_free(void* context);
void kia_protocol_decoder_v1_reset(void* context);
void kia_protocol_decoder_v1_feed(void* context, bool level, uint32_t duration);
uint8_t kia_protocol_decoder_v1_get_hash_data(void* context);
SubGhzProtocolStatus kia_protocol_decoder_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    kia_protocol_decoder_v1_deserialize(void* context, FlipperFormat* flipper_format);
void kia_protocol_decoder_v1_get_string(void* context, FuriString* output);
