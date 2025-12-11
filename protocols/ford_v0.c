#include "ford_v0.h"

#define TAG "FordProtocolV0"

static const SubGhzBlockConst subghz_protocol_ford_v0_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

typedef struct SubGhzProtocolDecoderFordV0
{
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;

    uint64_t data_low;
    uint64_t data_high;
    uint8_t bit_count;

    uint16_t header_count;

    uint64_t key1;
    uint16_t key2;
    uint32_t serial;
    uint8_t button;
    uint32_t count;
} SubGhzProtocolDecoderFordV0;

typedef struct SubGhzProtocolEncoderFordV0
{
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzProtocolBlockGeneric generic;

    uint16_t key2;
    bool is_running;
    size_t preamble_count;
    size_t data_bit_index;
    bool send_high;
} SubGhzProtocolEncoderFordV0;

void* subghz_protocol_encoder_ford_v0_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_ford_v0_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_ford_v0_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_ford_v0_stop(void* context);
LevelDuration subghz_protocol_encoder_ford_v0_yield(void* context);

typedef enum
{
    FordV0DecoderStepReset = 0,
    FordV0DecoderStepPreamble,
    FordV0DecoderStepPreambleCheck,
    FordV0DecoderStepGap,
    FordV0DecoderStepData,
} FordV0DecoderStep;

// Forward declarations
static void ford_v0_add_bit(SubGhzProtocolDecoderFordV0 *instance, bool bit);
static void decode_ford_v0(uint64_t key1, uint16_t key2, uint32_t *serial, uint8_t *button, uint32_t *count);
static bool ford_v0_process_data(SubGhzProtocolDecoderFordV0 *instance);

const SubGhzProtocolDecoder subghz_protocol_ford_v0_decoder = {
    .alloc = subghz_protocol_decoder_ford_v0_alloc,
    .free = subghz_protocol_decoder_ford_v0_free,
    .feed = subghz_protocol_decoder_ford_v0_feed,
    .reset = subghz_protocol_decoder_ford_v0_reset,
    .get_hash_data = subghz_protocol_decoder_ford_v0_get_hash_data,
    .serialize = subghz_protocol_decoder_ford_v0_serialize,
    .deserialize = subghz_protocol_decoder_ford_v0_deserialize,
    .get_string = subghz_protocol_decoder_ford_v0_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_ford_v0_encoder = {
    .alloc = subghz_protocol_encoder_ford_v0_alloc,
    .free = subghz_protocol_encoder_ford_v0_free,
    .deserialize = subghz_protocol_encoder_ford_v0_deserialize,
    .stop = subghz_protocol_encoder_ford_v0_stop,
    .yield = subghz_protocol_encoder_ford_v0_yield,
};

const SubGhzProtocol ford_protocol_v0 = {
    .name = FORD_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable,
    .decoder = &subghz_protocol_ford_v0_decoder,
    .encoder = &subghz_protocol_ford_v0_encoder,
};

void* subghz_protocol_encoder_ford_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFordV0* instance = malloc(sizeof(SubGhzProtocolEncoderFordV0));
    instance->base.protocol = &ford_protocol_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_encoder_ford_v0_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFordV0* instance = context;
    free(instance);
}

static void ford_v0_add_bit(SubGhzProtocolDecoderFordV0 *instance, bool bit)
{
    uint32_t low = (uint32_t)instance->data_low;
    instance->data_low = (instance->data_low << 1) | (bit ? 1 : 0);
    instance->data_high = (instance->data_high << 1) | ((low >> 31) & 1);
    instance->bit_count++;
}

static void decode_ford_v0(uint64_t key1, uint16_t key2, uint32_t *serial, uint8_t *button, uint32_t *count)
{
    uint8_t buf[13] = {0};

    for (int i = 0; i < 8; ++i)
    {
        buf[i] = (uint8_t)(key1 >> (56 - i * 8));
    }

    buf[8] = (uint8_t)(key2 >> 8);
    buf[9] = (uint8_t)(key2 & 0xFF);

    uint8_t tmp = buf[8];
    uint8_t parity = 0;
    uint8_t parity_any = (tmp != 0);
    while (tmp)
    {
        parity ^= (tmp & 1);
        tmp >>= 1;
    }
    buf[11] = parity_any ? parity : 0;

    uint8_t xor_byte;
    uint8_t limit;
    if (buf[11])
    {
        xor_byte = buf[7];
        limit = 7;
    }
    else
    {
        xor_byte = buf[6];
        limit = 6;
    }

    for (int idx = 1; idx < limit; ++idx)
    {
        buf[idx] ^= xor_byte;
    }

    if (buf[11] == 0)
    {
        buf[7] ^= xor_byte;
    }

    uint8_t orig_b7 = buf[7];

    buf[7] = (orig_b7 & 0xAA) | (buf[6] & 0x55);
    uint8_t mixed = (buf[6] & 0xAA) | (orig_b7 & 0x55);
    buf[12] = mixed;
    buf[6] = mixed;

    uint32_t serial_le = ((uint32_t)buf[1]) |
                         ((uint32_t)buf[2] << 8) |
                         ((uint32_t)buf[3] << 16) |
                         ((uint32_t)buf[4] << 24);

    *serial = ((serial_le & 0xFF) << 24) |
              (((serial_le >> 8) & 0xFF) << 16) |
              (((serial_le >> 16) & 0xFF) << 8) |
              ((serial_le >> 24) & 0xFF);

    *button = (buf[5] >> 4) & 0x0F;

    *count = ((buf[5] & 0x0F) << 16) |
             (buf[6] << 8) |
             buf[7];
}

static bool ford_v0_process_data(SubGhzProtocolDecoderFordV0 *instance)
{
    if (instance->bit_count == 64)
    {
        uint64_t combined = ((uint64_t)instance->data_high << 32) | instance->data_low;
        instance->key1 = ~combined;
        instance->data_low = 0;
        instance->data_high = 0;
        return false;
    }

    if (instance->bit_count == 80)
    {
        uint16_t key2_raw = (uint16_t)(instance->data_low & 0xFFFF);
        uint16_t key2 = ~key2_raw;

        decode_ford_v0(instance->key1, key2, &instance->serial, &instance->button, &instance->count);
        instance->key2 = key2;
        return true;
    }

    return false;
}

void *subghz_protocol_decoder_ford_v0_alloc(SubGhzEnvironment *environment)
{
    UNUSED(environment);
    SubGhzProtocolDecoderFordV0 *instance = malloc(sizeof(SubGhzProtocolDecoderFordV0));
    instance->base.protocol = &ford_protocol_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_ford_v0_free(void *context)
{
    furi_assert(context);
    SubGhzProtocolDecoderFordV0 *instance = context;
    free(instance);
}

void subghz_protocol_decoder_ford_v0_reset(void *context)
{
    furi_assert(context);
    SubGhzProtocolDecoderFordV0 *instance = context;
    instance->decoder.parser_step = FordV0DecoderStepReset;
    instance->decoder.te_last = 0;
    instance->manchester_state = ManchesterStateMid1;
    instance->data_low = 0;
    instance->data_high = 0;
    instance->bit_count = 0;
    instance->header_count = 0;
    instance->key1 = 0;
    instance->key2 = 0;
    instance->serial = 0;
    instance->button = 0;
    instance->count = 0;
}

void subghz_protocol_decoder_ford_v0_feed(void *context, bool level, uint32_t duration)
{
    furi_assert(context);
    SubGhzProtocolDecoderFordV0 *instance = context;

    uint32_t te_short = subghz_protocol_ford_v0_const.te_short;
    uint32_t te_long = subghz_protocol_ford_v0_const.te_long;
    uint32_t te_delta = subghz_protocol_ford_v0_const.te_delta;
    uint32_t gap_threshold = 3500;

    switch (instance->decoder.parser_step)
    {
    case FordV0DecoderStepReset:
        if (level && (DURATION_DIFF(duration, te_short) < te_delta))
        {
            instance->data_low = 0;
            instance->data_high = 0;
            instance->decoder.parser_step = FordV0DecoderStepPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            instance->bit_count = 0;
            manchester_advance(instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
        }
        break;

    case FordV0DecoderStepPreamble:
        if (!level)
        {
            if (DURATION_DIFF(duration, te_long) < te_delta)
            {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = FordV0DecoderStepPreambleCheck;
            }
            else
            {
                instance->decoder.parser_step = FordV0DecoderStepReset;
            }
        }
        break;

    case FordV0DecoderStepPreambleCheck:
        if (level)
        {
            if (DURATION_DIFF(duration, te_long) < te_delta)
            {
                instance->header_count++;
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = FordV0DecoderStepPreamble;
            }
            else if (DURATION_DIFF(duration, te_short) < te_delta)
            {
                instance->decoder.parser_step = FordV0DecoderStepGap;
            }
            else
            {
                instance->decoder.parser_step = FordV0DecoderStepReset;
            }
        }
        break;

    case FordV0DecoderStepGap:
        if (!level && (DURATION_DIFF(duration, gap_threshold) < 250))
        {
            instance->data_low = 1;
            instance->data_high = 0;
            instance->bit_count = 1;
            instance->decoder.parser_step = FordV0DecoderStepData;
        }
        else if (!level && duration > gap_threshold + 250)
        {
            instance->decoder.parser_step = FordV0DecoderStepReset;
        }
        break;

    case FordV0DecoderStepData:
    {
        ManchesterEvent event;

        if (DURATION_DIFF(duration, te_short) < te_delta)
        {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
        }
        else if (DURATION_DIFF(duration, te_long) < te_delta)
        {
            event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
        }
        else
        {
            instance->decoder.parser_step = FordV0DecoderStepReset;
            break;
        }

        bool data_bit;
        if (manchester_advance(instance->manchester_state, event, &instance->manchester_state, &data_bit))
        {
            ford_v0_add_bit(instance, data_bit);

            if (ford_v0_process_data(instance))
            {
                instance->generic.data = instance->key1;
                instance->generic.data_count_bit = 64;
                instance->generic.serial = instance->serial;
                instance->generic.btn = instance->button;
                instance->generic.cnt = instance->count;

                if (instance->base.callback)
                {
                    instance->base.callback(&instance->base, instance->base.context);
                }

                instance->data_low = 0;
                instance->data_high = 0;
                instance->bit_count = 0;
                instance->decoder.parser_step = FordV0DecoderStepReset;
            }
        }

        instance->decoder.te_last = duration;
        break;
    }
    }
}

uint8_t subghz_protocol_decoder_ford_v0_get_hash_data(void *context)
{
    furi_assert(context);
    SubGhzProtocolDecoderFordV0 *instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_ford_v0_serialize(
    void *context,
    FlipperFormat *flipper_format,
    SubGhzRadioPreset *preset)
{
    furi_assert(context);
    SubGhzProtocolDecoderFordV0 *instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    ret = subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if (ret == SubGhzProtocolStatusOk)
    {
        // Add Ford-specific data
        uint32_t temp = (instance->key2 >> 8) & 0xFF; // BS byte
        flipper_format_write_uint32(flipper_format, "BS", &temp, 1);

        temp = instance->key2 & 0xFF; // CRC byte
        flipper_format_write_uint32(flipper_format, "CRC", &temp, 1);

        // Ensure serial, button, count are saved
        flipper_format_write_uint32(flipper_format, "Serial", &instance->serial, 1);

        temp = instance->button;
        flipper_format_write_uint32(flipper_format, "Btn", &temp, 1);

        flipper_format_write_uint32(flipper_format, "Cnt", &instance->count, 1);
    }

    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_ford_v0_deserialize(void *context, FlipperFormat *flipper_format)
{
    furi_assert(context);
    SubGhzProtocolDecoderFordV0 *instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_ford_v0_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_ford_v0_get_string(void *context, FuriString *output)
{
    furi_assert(context);
    SubGhzProtocolDecoderFordV0 *instance = context;

    uint32_t code_found_hi = (uint32_t)(instance->key1 >> 32);
    uint32_t code_found_lo = (uint32_t)(instance->key1 & 0xFFFFFFFF);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%08lX Btn:%02X Cnt:%06lX\r\n"
        "BS:%02X CRC:%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        instance->serial,
        instance->button,
        instance->count,
        (instance->key2 >> 8) & 0xFF,
        instance->key2 & 0xFF);
}

static void encode_ford_v0(uint32_t serial, uint8_t button, uint32_t count, uint64_t* key1, uint16_t* key2) {
    uint8_t buf[13] = {0};

    // Populate buf with serial, button, count
    buf[1] = (serial >> 24) & 0xFF;
    buf[2] = (serial >> 16) & 0xFF;
    buf[3] = (serial >> 8) & 0xFF;
    buf[4] = serial & 0xFF;
    buf[5] = (button << 4) | ((count >> 16) & 0x0F);
    buf[6] = (count >> 8) & 0xFF;
    buf[7] = count & 0xFF;

    // Reverse the process of decode_ford_v0
    uint8_t orig_b7 = buf[7];
    uint8_t mixed = buf[6];
    buf[7] = (orig_b7 & 0xAA) | (mixed & 0xAA);
    buf[6] = (mixed & 0x55) | (orig_b7 & 0x55);

    uint8_t xor_byte;
    uint8_t limit;

    // Simplified parity logic for encoding
    bool use_b7 = (serial % 2 == 0); // Example logic
    if (use_b7)
    {
        xor_byte = buf[7];
        limit = 7;
        buf[8] = 1; // Non-zero BS to indicate parity
    }
    else
    {
        xor_byte = buf[6];
        limit = 6;
        buf[8] = 0; // BS=0
    }

    for (int idx = 1; idx < limit; ++idx)
    {
        buf[idx] ^= xor_byte;
    }

    if (!use_b7)
    {
        buf[7] ^= xor_byte;
    }

    // CRC is simplified here
    buf[9] = 0xAA;

    *key1 = 0;
    for(int i = 0; i < 8; i++) {
        *key1 = (*key1 << 8) | buf[i];
    }

    *key2 = (buf[8] << 8) | buf[9];

    *key1 = ~(*key1);
    *key2 = ~(*key2);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_ford_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderFordV0* instance = context;
    SubGhzProtocolStatus res = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_ford_v0_const.min_count_bit_for_found);
    if(res == SubGhzProtocolStatusOk) {
        flipper_format_read_uint32(flipper_format, "Serial", &instance->generic.serial, 1);
        uint32_t btn_temp;
        flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1);
        instance->generic.btn = (uint8_t)btn_temp;
        flipper_format_read_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1);
    }
    return res;
}

void subghz_protocol_encoder_ford_v0_stop(void* context) {
    SubGhzProtocolEncoderFordV0* instance = context;
    instance->is_running = false;
}

LevelDuration subghz_protocol_encoder_ford_v0_yield(void* context) {
    SubGhzProtocolEncoderFordV0* instance = context;

    if(!instance->is_running) {
        instance->is_running = true;
        instance->preamble_count = 0;
        instance->data_bit_index = 0;

        uint64_t key1;
        uint16_t key2;
        encode_ford_v0(instance->generic.serial, instance->generic.btn, instance->generic.cnt, &key1, &key2);
        instance->generic.data = key1;
        instance->key2 = key2;
    }

    // Preamble
    if(instance->preamble_count < 20) {
        instance->preamble_count++;
        if(instance->preamble_count % 2 != 0) {
            return level_duration_make(true, subghz_protocol_ford_v0_const.te_long);
        } else {
            return level_duration_make(false, subghz_protocol_ford_v0_const.te_long);
        }
    }

    // Data
    if(instance->data_bit_index < 80) {
        uint64_t data_to_send = instance->generic.data;
        if(instance->data_bit_index >= 64) {
            data_to_send = instance->key2;
        }

        uint64_t bit_mask = 1ULL << (63 - (instance->data_bit_index % 64));
        bool bit = (data_to_send & bit_mask) ? 1 : 0;
        instance->data_bit_index++;

        if(bit) {
            return level_duration_make(true, subghz_protocol_ford_v0_const.te_short);
        } else {
            return level_duration_make(true, subghz_protocol_ford_v0_const.te_long);
        }
    }

    subghz_protocol_encoder_ford_v0_stop(context);
    return level_duration_reset();
}