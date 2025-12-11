#include "kia_v5.h"

#define TAG "KiaV5"

static const SubGhzBlockConst kia_protocol_v5_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 150,
    .min_count_bit_for_found = 64,
};

struct SubGhzProtocolDecoderKiaV5
{
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    uint8_t raw_bits[32];
    uint16_t raw_bit_count;
};

struct SubGhzProtocolEncoderKiaV5
{
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    bool is_running;
    size_t preamble_count;
    size_t data_bit_index;
    bool send_high;
};

void* kia_protocol_encoder_v5_alloc(SubGhzEnvironment* environment);
void kia_protocol_encoder_v5_free(void* context);
SubGhzProtocolStatus
    kia_protocol_encoder_v5_deserialize(void* context, FlipperFormat* flipper_format);
void kia_protocol_encoder_v5_stop(void* context);
LevelDuration kia_protocol_encoder_v5_yield(void* context);

typedef enum
{
    KiaV5DecoderStepReset = 0,
    KiaV5DecoderStepCheckPreamble,
    KiaV5DecoderStepCollectRawBits,
} KiaV5DecoderStep;

const SubGhzProtocolDecoder kia_protocol_v5_decoder = {
    .alloc = kia_protocol_decoder_v5_alloc,
    .free = kia_protocol_decoder_v5_free,
    .feed = kia_protocol_decoder_v5_feed,
    .reset = kia_protocol_decoder_v5_reset,
    .get_hash_data = kia_protocol_decoder_v5_get_hash_data,
    .serialize = kia_protocol_decoder_v5_serialize,
    .deserialize = kia_protocol_decoder_v5_deserialize,
    .get_string = kia_protocol_decoder_v5_get_string,
};

const SubGhzProtocolEncoder kia_protocol_v5_encoder = {
    .alloc = kia_protocol_encoder_v5_alloc,
    .free = kia_protocol_encoder_v5_free,
    .deserialize = kia_protocol_encoder_v5_deserialize,
    .stop = kia_protocol_encoder_v5_stop,
    .yield = kia_protocol_encoder_v5_yield,
};

const SubGhzProtocol kia_protocol_v5 = {
    .name = KIA_PROTOCOL_V5_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable,
    .decoder = &kia_protocol_v5_decoder,
    .encoder = &kia_protocol_v5_encoder,
};

void* kia_protocol_encoder_v5_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderKiaV5* instance = malloc(sizeof(SubGhzProtocolEncoderKiaV5));
    instance->base.protocol = &kia_protocol_v5;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void kia_protocol_encoder_v5_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    free(instance);
}

static void kia_v5_add_raw_bit(SubGhzProtocolDecoderKiaV5 *instance, bool bit)
{
    if (instance->raw_bit_count < 256)
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

static inline bool kia_v5_get_raw_bit(SubGhzProtocolDecoderKiaV5 *instance, uint16_t idx)
{
    uint16_t byte_idx = idx / 8;
    uint8_t bit_idx = 7 - (idx % 8);
    return (instance->raw_bits[byte_idx] >> bit_idx) & 1;
}

static bool kia_v5_manchester_decode(SubGhzProtocolDecoderKiaV5 *instance)
{
    if (instance->raw_bit_count < 130)
    {
        return false;
    }

    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;

    // Start at offset 2 for proper Manchester alignment
    const uint16_t start_bit = 2;

    for (uint16_t i = start_bit;
         i + 1 < instance->raw_bit_count && instance->decoder.decode_count_bit < 64;
         i += 2)
    {
        bool bit1 = kia_v5_get_raw_bit(instance, i);
        bool bit2 = kia_v5_get_raw_bit(instance, i + 1);

        uint8_t two_bits = (bit1 << 1) | bit2;

        if (two_bits == 0x01)
        { // 01 = decoded 1
            instance->decoder.decode_data = (instance->decoder.decode_data << 1) | 1;
            instance->decoder.decode_count_bit++;
        }
        else if (two_bits == 0x02)
        { // 10 = decoded 0
            instance->decoder.decode_data = (instance->decoder.decode_data << 1);
            instance->decoder.decode_count_bit++;
        }
        else
        {
            break;
        }
    }

    return instance->decoder.decode_count_bit >= kia_protocol_v5_const.min_count_bit_for_found;
}

void *kia_protocol_decoder_v5_alloc(SubGhzEnvironment *environment)
{
    UNUSED(environment);
    SubGhzProtocolDecoderKiaV5 *instance = malloc(sizeof(SubGhzProtocolDecoderKiaV5));
    instance->base.protocol = &kia_protocol_v5;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void kia_protocol_decoder_v5_free(void *context)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5 *instance = context;
    free(instance);
}

void kia_protocol_decoder_v5_reset(void *context)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5 *instance = context;
    instance->decoder.parser_step = KiaV5DecoderStepReset;
    instance->header_count = 0;
    instance->raw_bit_count = 0;
    memset(instance->raw_bits, 0, sizeof(instance->raw_bits));
}

void kia_protocol_decoder_v5_feed(void *context, bool level, uint32_t duration)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5 *instance = context;

    switch (instance->decoder.parser_step)
    {
    case KiaV5DecoderStepReset:
        if ((level) && (DURATION_DIFF(duration, kia_protocol_v5_const.te_short) <
                        kia_protocol_v5_const.te_delta))
        {
            instance->decoder.parser_step = KiaV5DecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 1;
        }
        break;

    case KiaV5DecoderStepCheckPreamble:
        if (level)
        {
            if ((DURATION_DIFF(duration, kia_protocol_v5_const.te_short) <
                 kia_protocol_v5_const.te_delta) ||
                (DURATION_DIFF(duration, kia_protocol_v5_const.te_long) <
                 kia_protocol_v5_const.te_delta))
            {
                instance->decoder.te_last = duration;
            }
            else
            {
                instance->decoder.parser_step = KiaV5DecoderStepReset;
            }
        }
        else
        {
            if ((DURATION_DIFF(duration, kia_protocol_v5_const.te_short) <
                 kia_protocol_v5_const.te_delta) &&
                (DURATION_DIFF(instance->decoder.te_last, kia_protocol_v5_const.te_short) <
                 kia_protocol_v5_const.te_delta))
            {
                instance->header_count++;
            }
            else if (
                (DURATION_DIFF(duration, kia_protocol_v5_const.te_long) <
                 kia_protocol_v5_const.te_delta) &&
                (DURATION_DIFF(instance->decoder.te_last, kia_protocol_v5_const.te_short) <
                 kia_protocol_v5_const.te_delta))
            {
                if (instance->header_count > 40)
                {
                    instance->decoder.parser_step = KiaV5DecoderStepCollectRawBits;
                    instance->raw_bit_count = 0;
                    memset(instance->raw_bits, 0, sizeof(instance->raw_bits));
                }
                else
                {
                    instance->header_count++;
                }
            }
            else if (
                DURATION_DIFF(instance->decoder.te_last, kia_protocol_v5_const.te_long) <
                kia_protocol_v5_const.te_delta)
            {
                instance->header_count++;
            }
            else
            {
                instance->decoder.parser_step = KiaV5DecoderStepReset;
            }
        }
        break;

    case KiaV5DecoderStepCollectRawBits:
        if (duration > 1200)
        {
            if (kia_v5_manchester_decode(instance))
            {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                // Compute yek (bit-reverse each byte)
                uint64_t yek = 0;
                for (int i = 0; i < 8; i++)
                {
                    uint8_t byte = (instance->generic.data >> (i * 8)) & 0xFF;
                    uint8_t reversed = 0;
                    for (int b = 0; b < 8; b++)
                    {
                        if (byte & (1 << b))
                            reversed |= (1 << (7 - b));
                    }
                    yek |= ((uint64_t)reversed << ((7 - i) * 8));
                }

                // Shift serial right by 1 to correct alignment
                instance->generic.serial = (uint32_t)(((yek >> 32) & 0x0FFFFFFF) >> 1);
                instance->generic.btn = (uint8_t)((yek >> 61) & 0x07); // Shift btn too
                instance->generic.cnt = (uint16_t)(yek & 0xFFFF);

                FURI_LOG_I(
                    TAG,
                    "Key=%08lX%08lX Sn=%07lX Btn=%X",
                    (uint32_t)(instance->generic.data >> 32),
                    (uint32_t)(instance->generic.data & 0xFFFFFFFF),
                    instance->generic.serial,
                    instance->generic.btn);

                if (instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }

            instance->decoder.parser_step = KiaV5DecoderStepReset;
            break;
        }

        int num_bits = 0;
        if (DURATION_DIFF(duration, kia_protocol_v5_const.te_short) <
            kia_protocol_v5_const.te_delta)
        {
            num_bits = 1;
        }
        else if (
            DURATION_DIFF(duration, kia_protocol_v5_const.te_long) <
            kia_protocol_v5_const.te_delta)
        {
            num_bits = 2;
        }
        else
        {
            instance->decoder.parser_step = KiaV5DecoderStepReset;
            break;
        }

        for (int i = 0; i < num_bits; i++)
        {
            kia_v5_add_raw_bit(instance, level);
        }

        break;
    }
}

uint8_t kia_protocol_decoder_v5_get_hash_data(void *context)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5 *instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus kia_protocol_decoder_v5_serialize(
    void *context,
    FlipperFormat *flipper_format,
    SubGhzRadioPreset *preset)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5 *instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    ret = subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if (ret == SubGhzProtocolStatusOk)
    {
        // Save decoded fields
        flipper_format_write_uint32(flipper_format, "Serial", &instance->generic.serial, 1);

        uint32_t temp = instance->generic.btn;
        flipper_format_write_uint32(flipper_format, "Btn", &temp, 1);

        flipper_format_write_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1);

        // Save raw bit data for exact reproduction (since V5 has complex bit reversal)
        uint32_t raw_high = (uint32_t)(instance->generic.data >> 32);
        uint32_t raw_low = (uint32_t)(instance->generic.data & 0xFFFFFFFF);
        flipper_format_write_uint32(flipper_format, "DataHi", &raw_high, 1);
        flipper_format_write_uint32(flipper_format, "DataLo", &raw_low, 1);
    }

    return ret;
}

SubGhzProtocolStatus
kia_protocol_decoder_v5_deserialize(void *context, FlipperFormat *flipper_format)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5 *instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, kia_protocol_v5_const.min_count_bit_for_found);
}

void kia_protocol_decoder_v5_get_string(void *context, FuriString *output)
{
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5 *instance = context;

    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%07lX Btn:%X Cnt:%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

SubGhzProtocolStatus
    kia_protocol_encoder_v5_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    SubGhzProtocolStatus res = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, kia_protocol_v5_const.min_count_bit_for_found);
    if(res == SubGhzProtocolStatusOk) {
        flipper_format_read_uint32(flipper_format, "Serial", &instance->generic.serial, 1);
        uint32_t btn_temp;
        flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1);
        instance->generic.btn = (uint8_t)btn_temp;
        flipper_format_read_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1);
    }
    return res;
}

void kia_protocol_encoder_v5_stop(void* context) {
    SubGhzProtocolEncoderKiaV5* instance = context;
    instance->is_running = false;
}

LevelDuration kia_protocol_encoder_v5_yield(void* context) {
    SubGhzProtocolEncoderKiaV5* instance = context;

    if(!instance->is_running) {
        instance->is_running = true;
        instance->preamble_count = 0;
        instance->data_bit_index = 0;
        instance->send_high = true;

        // Reconstruct yek
        uint64_t serial_shifted = (uint64_t)instance->generic.serial << 1;
        uint64_t yek = ((uint64_t)instance->generic.btn << 61) |
                       (serial_shifted << 32) |
                       (instance->generic.cnt);

        // Bit-reverse each byte to get the final data
        uint64_t data = 0;
        for (int i = 0; i < 8; i++)
        {
            uint8_t byte = (yek >> ((7 - i) * 8)) & 0xFF;
            uint8_t reversed = 0;
            for (int b = 0; b < 8; b++)
            {
                if (byte & (1 << b))
                    reversed |= (1 << (7 - b));
            }
            data |= ((uint64_t)reversed << (i * 8));
        }
        instance->generic.data = data;
    }

    // Preamble
    if(instance->preamble_count < 42) {
        instance->preamble_count++;
        if(instance->preamble_count % 2 != 0) {
            return level_duration_make(true, kia_protocol_v5_const.te_short);
        } else {
            return level_duration_make(false, kia_protocol_v5_const.te_short);
        }
    }

    // Sync
    if(instance->preamble_count == 42) {
        instance->preamble_count++;
        return level_duration_make(false, kia_protocol_v5_const.te_long);
    }

    // Data
    if(instance->data_bit_index < 64) {
        uint64_t bit_mask = 1ULL << (63 - instance->data_bit_index);
        bool bit = (instance->generic.data & bit_mask) ? 1 : 0;

        if(instance->send_high) {
            instance->send_high = false;
            return level_duration_make(bit, kia_protocol_v5_const.te_short);
        } else {
            instance->send_high = true;
            instance->data_bit_index++;
            return level_duration_make(!bit, kia_protocol_v5_const.te_short);
        }
    }

    kia_protocol_encoder_v5_stop(context);
    return level_duration_reset();
}
