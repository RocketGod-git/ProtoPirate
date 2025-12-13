#pragma once

#include "kia_generic.h"

#define KIA_PROTOCOL_V0_NAME "Kia V0"

typedef struct SubGhzProtocolDecoderKIA SubGhzProtocolDecoderKIA;
typedef struct SubGhzProtocolEncoderKIA SubGhzProtocolEncoderKIA;

extern const SubGhzProtocolDecoder subghz_protocol_kia_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_kia_encoder;
extern const SubGhzProtocol kia_protocol_v0;

void* subghz_protocol_decoder_kia_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_kia_free(void* context);
void subghz_protocol_decoder_kia_reset(void* context);
void subghz_protocol_decoder_kia_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_kia_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_kia_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus subghz_protocol_decoder_kia_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_kia_get_string(void* context, FuriString* output);