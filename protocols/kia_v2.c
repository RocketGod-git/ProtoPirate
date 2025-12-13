#include "kia_v2.h"

#define TAG "KiaV2"

static const SubGhzBlockConst kia_protocol_v2_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 150,
    .min_count_bit_for_found = 51,
};

struct SubGhzProtocolDecoderKiaV2
{
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    uint8_t raw_bits[20];
    uint16_t raw_bit_count;
};

struct SubGhzProtocolEncoderKiaV2
{
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t crc;
    uint16_t raw_cnt;
    bool is_running;
    size_t preamble_count;
    size_t data_bit_index;
    bool send_high;
};

void* kia_protocol_encoder_v2_alloc(SubGhzEnvironment* environment);
void kia_protocol_encoder_v2_free(void* context);
SubGhzProtocolStatus
    kia_protocol_encoder_v2_deserialize(void* context, FlipperFormat* flipper_format);
void kia_protocol_encoder_v2_stop(void* context);
LevelDuration kia_protocol_encoder_v2_yield(void* context);

typedef enum
{
    KiaV2DecoderStepReset = 0,
    KiaV2DecoderStepCheckPreamble,
    KiaV2DecoderStepCollectRawBits,
} KiaV2DecoderStep;

const SubGhzProtocolDecoder kia_protocol_v2_decoder = {
    .alloc = kia_protocol_decoder_v2_alloc,
    .free = kia_protocol_decoder_v2_free,
    .feed = kia_protocol_decoder_v2_feed,
    .reset = kia_protocol_decoder_v2_reset,
    .get_hash_data = kia_protocol_decoder_v2_get_hash_data,
    .serialize = kia_protocol_decoder_v2_serialize,
    .deserialize = kia_protocol_decoder_v2_deserialize,
    .get_string = kia_protocol_decoder_v2_get_string,
};

const SubGhzProtocolEncoder kia_protocol_v2_encoder = {
    .alloc = kia_protocol_encoder_v2_alloc,
    .free = kia_protocol_encoder_v2_free,
    .deserialize = kia_protocol_encoder_v2_deserialize,
    .stop = kia_protocol_encoder_v2_stop,
    .yield = kia_protocol_encoder_v2_yield,
};

const SubGhzProtocol kia_protocol_v2 = {
    .name = KIA_PROTOCOL_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable,
    .decoder = &kia_protocol_v2_decoder,
    .encoder = &kia_protocol_v2_encoder,
};

void* kia_protocol_encoder_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderKiaV2* instance = malloc(sizeof(SubGhzProtocolEncoderKiaV2));
    instance->base.protocol = &kia_protocol_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void kia_protocol_encoder_v2_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV2* instance = context;
    free(instance);
}

static void kia_v2_add_raw_bit(SubGhzProtocolDecoderKiaV2 *instance, bool bit)
{
    if (instance->raw_bit_count < 160)
    {
        uint16_t byte_idx = instance->raw_bit_count / 8;
        uint8_t bit_idx = 7 - (instance->raw_bit_count % 8);
        if (bit)
        {
            instance->raw_bits[byte_idx] |= (1 << bit_idx);
        }
        else
        {
            instance->raw_bits[byte_idx] &= ~(1 << bit_idx);
        }
        instance->raw_bit_count++;
    }
}

static inline bool kia_v2_get_raw_bit(SubGhzProtocolDecoderKiaV2 *instance, uint16_t idx)
{
    uint16_t byte_idx = idx / 8;
    uint8_t bit_idx = 7 - (idx % 8);
    return (instance->raw_bits[byte_idx] >> bit_idx) & 1;
}

static bool kia_v2_manchester_decode(SubGhzProtocolDecoderKiaV2 *instance)
{
    if (instance->raw_bit_count < 100)
    {
        return false;
    }

    uint16_t best_bits = 0;
    uint64_t best_data = 0;

    for (uint16_t offset = 0; offset < 8; offset++)
    {
        uint64_t data = 0;
        uint16_t decoded_bits = 0;

        for (uint16_t i = offset; i + 1 < instance->raw_bit_count && decoded_bits < 53; i += 2)
        {
            bool bit1 = kia_v2_get_raw_bit(instance, i);
            bool bit2 = kia_v2_get_raw_bit(instance, i + 1);

            uint8_t two_bits = (bit1 << 1) | bit2;

            if (two_bits == 0x02)
            {
                data = (data << 1) | 1;
                decoded_bits++;
            }
            else if (two_bits == 0x01)
            {
                data = (data << 1);
                decoded_bits++;
            }
            else
            {
                break;
            }
        }

        if (decoded_bits > best_bits)
        {
            best_bits = decoded_bits;
            best_data = data;
        }
    }

    instance->decoder.decode_data = best_data;
    instance->decoder.decode_count_bit = best_bits;

    return best_bits >= kia_protocol_v2_const.min_count_bit_for_found;
}

void *kia_protocol_decoder_v2_alloc(SubGhzEnvironment *environment)
{
    UNUSED(environment);
    SubGhzProtocolDecoderKiaV2 *instance = malloc(sizeof(SubGhzProtocolDecoderKiaV2));
    instance->base.protocol = &kia_protocol_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void kia_protocol_decoder_v2_free(void *context)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2 *instance = context;
    free(instance);
}

void kia_protocol_decoder_v2_reset(void *context)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2 *instance = context;
    instance->decoder.parser_step = KiaV2DecoderStepReset;
    instance->header_count = 0;
    instance->raw_bit_count = 0;
    memset(instance->raw_bits, 0, sizeof(instance->raw_bits));
}

void kia_protocol_decoder_v2_feed(void *context, bool level, uint32_t duration)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2 *instance = context;

    switch (instance->decoder.parser_step)
    {
    case KiaV2DecoderStepReset:
        if ((level) && (DURATION_DIFF(duration, kia_protocol_v2_const.te_long) <
                        kia_protocol_v2_const.te_delta))
        {
            instance->decoder.parser_step = KiaV2DecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 1;
        }
        break;

    case KiaV2DecoderStepCheckPreamble:
        if (level)
        {
            if (DURATION_DIFF(duration, kia_protocol_v2_const.te_long) <
                kia_protocol_v2_const.te_delta)
            {
                instance->decoder.te_last = duration;
                instance->header_count++;
            }
            else if (
                DURATION_DIFF(duration, kia_protocol_v2_const.te_short) <
                kia_protocol_v2_const.te_delta)
            {
                instance->decoder.te_last = duration;
            }
            else
            {
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        }
        else
        {
            if (DURATION_DIFF(duration, kia_protocol_v2_const.te_long) <
                kia_protocol_v2_const.te_delta)
            {
                instance->header_count++;
            }
            else if (
                DURATION_DIFF(duration, kia_protocol_v2_const.te_short) <
                kia_protocol_v2_const.te_delta)
            {
                if (instance->header_count > 10 &&
                    DURATION_DIFF(instance->decoder.te_last, kia_protocol_v2_const.te_short) <
                        kia_protocol_v2_const.te_delta)
                {
                    instance->decoder.parser_step = KiaV2DecoderStepCollectRawBits;
                    instance->raw_bit_count = 0;
                    memset(instance->raw_bits, 0, sizeof(instance->raw_bits));
                }
            }
            else
            {
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        }
        break;

    case KiaV2DecoderStepCollectRawBits:
        if (duration > 1500)
        {
            if (kia_v2_manchester_decode(instance))
            {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                instance->generic.serial = (uint32_t)((instance->generic.data >> 20) & 0xFFFFFFFF);
                instance->generic.btn = (uint8_t)((instance->generic.data >> 16) & 0x0F);

                uint16_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
                instance->generic.cnt = ((raw_count >> 4) | (raw_count << 8)) & 0xFFF;

                if (instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }

            instance->decoder.parser_step = KiaV2DecoderStepReset;
            break;
        }

        int num_bits = 0;
        if (DURATION_DIFF(duration, kia_protocol_v2_const.te_short) <
            kia_protocol_v2_const.te_delta)
        {
            num_bits = 1;
        }
        else if (
            DURATION_DIFF(duration, kia_protocol_v2_const.te_long) <
            kia_protocol_v2_const.te_delta)
        {
            num_bits = 2;
        }
        else
        {
            instance->decoder.parser_step = KiaV2DecoderStepReset;
            break;
        }

        for (int i = 0; i < num_bits; i++)
        {
            kia_v2_add_raw_bit(instance, level);
        }

        break;
    }
}

uint8_t kia_protocol_decoder_v2_get_hash_data(void *context)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2 *instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus kia_protocol_decoder_v2_serialize(
    void *context,
    FlipperFormat *flipper_format,
    SubGhzRadioPreset *preset)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2 *instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    ret = subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if (ret == SubGhzProtocolStatusOk)
    {
        // Save CRC (last nibble)
        uint32_t crc = instance->generic.data & 0x0F;
        flipper_format_write_uint32(flipper_format, "CRC", &crc, 1);

        // Save decoded fields
        flipper_format_write_uint32(flipper_format, "Serial", &instance->generic.serial, 1);

        uint32_t temp = instance->generic.btn;
        flipper_format_write_uint32(flipper_format, "Btn", &temp, 1);

        flipper_format_write_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1);

        // Save raw count before transformation (for exact reproduction)
        uint32_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
        flipper_format_write_uint32(flipper_format, "RawCnt", &raw_count, 1);
    }

    return ret;
}

SubGhzProtocolStatus
kia_protocol_decoder_v2_deserialize(void *context, FlipperFormat *flipper_format)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2 *instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, kia_protocol_v2_const.min_count_bit_for_found);
}

void kia_protocol_decoder_v2_get_string(void *context, FuriString *output)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2 *instance = context;

    uint8_t crc = instance->generic.data & 0x0F;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%013llX\r\n"
        "Sn:%08lX Btn:%X\r\n"
        "Cnt:%03lX CRC:%X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt,
        crc);
}

SubGhzProtocolStatus
    kia_protocol_encoder_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV2* instance = context;
    SubGhzProtocolStatus res = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, kia_protocol_v2_const.min_count_bit_for_found);
    if(res == SubGhzProtocolStatusOk) {
        // Essential fields for re-encoding
        flipper_format_read_uint32(flipper_format, "Serial", &instance->generic.serial, 1);
        uint32_t btn_temp;
        flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1);
        instance->generic.btn = (uint8_t)btn_temp;
        flipper_format_read_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1);
        uint32_t crc_temp;
        flipper_format_read_uint32(flipper_format, "CRC", &crc_temp, 1);
        instance->crc = (uint8_t)crc_temp;
        uint32_t raw_cnt_temp;
        flipper_format_read_uint32(flipper_format, "RawCnt", &raw_cnt_temp, 1);
        instance->raw_cnt = (uint16_t)raw_cnt_temp;
    }
    return res;
}

void kia_protocol_encoder_v2_stop(void* context) {
    SubGhzProtocolEncoderKiaV2* instance = context;
    instance->is_running = false;
}

LevelDuration kia_protocol_encoder_v2_yield(void* context) {
    SubGhzProtocolEncoderKiaV2* instance = context;

    if(!instance->is_running) {
        // Start a new transmission
        instance->is_running = true;
        instance->preamble_count = 0;
        instance->data_bit_index = 0;
        instance->send_high = true;

        // Reconstruct the 51-bit data with updated fields and calculate CRC
        uint16_t raw_count = ((instance->generic.cnt << 4) | (instance->generic.cnt >> 8)) & 0xFFF;
        uint64_t data_without_crc = ((uint64_t)instance->generic.serial << 20) |
                                    ((uint64_t)instance->generic.btn << 16) |
                                    ((uint64_t)raw_count << 4);
        uint8_t crc = 0;
        for(int i = 0; i < 6; i++) {
            crc += (data_without_crc >> (8 * i)) & 0xFF;
        }
        instance->crc = crc & 0x0F;
        instance->generic.data = data_without_crc | instance->crc;
    }

    // Preamble
    if(instance->preamble_count < 12) {
        instance->preamble_count++;
        if(instance->preamble_count % 2 != 0) {
            return level_duration_make(true, kia_protocol_v2_const.te_long);
        } else {
            return level_duration_make(false, kia_protocol_v2_const.te_long);
        }
    }

    // Sync
    if(instance->preamble_count == 12) {
        instance->preamble_count++;
        return level_duration_make(true, kia_protocol_v2_const.te_short);
    }
    if(instance->preamble_count == 13) {
        instance->preamble_count++;
        return level_duration_make(false, kia_protocol_v2_const.te_short);
    }

    // Data bits
    if(instance->data_bit_index < instance->generic.data_count_bit) {
        // Manchester encoding: 1 -> 10, 0 -> 01
        uint64_t bit_mask = 1ULL << (instance->generic.data_count_bit - 1 - instance->data_bit_index);
        bool bit = (instance->generic.data & bit_mask) ? 1 : 0;

        if(instance->send_high) {
            instance->send_high = false;
            return level_duration_make(bit, kia_protocol_v2_const.te_short);
        } else {
            instance->send_high = true;
            instance->data_bit_index++;
            return level_duration_make(!bit, kia_protocol_v2_const.te_short);
        }
    }

    kia_protocol_encoder_v2_stop(context);
    return level_duration_reset();
}
