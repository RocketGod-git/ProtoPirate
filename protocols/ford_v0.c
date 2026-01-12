#include "ford_v0.h"

#define TAG "FordProtocolV0"

static const SubGhzBlockConst subghz_protocol_ford_v0_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

#define FORD_V0_PREAMBLE_PAIRS 64
#define FORD_V0_GAP_US 3500
#define FORD_V0_TOTAL_BURSTS 3

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
    SubGhzBlockGeneric generic;
    
    uint64_t key1;
    uint16_t key2;
    uint32_t serial;
    uint8_t button;
    uint32_t count;
} SubGhzProtocolEncoderFordV0;

typedef enum
{
    FordV0DecoderStepReset = 0,
    FordV0DecoderStepPreamble,
    FordV0DecoderStepPreambleCheck,
    FordV0DecoderStepGap,
    FordV0DecoderStepData,
} FordV0DecoderStep;

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
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_ford_v0_decoder,
    .encoder = &subghz_protocol_ford_v0_encoder,
};

// ============================================================================
// ENCODER IMPLEMENTATION
// ============================================================================

void *subghz_protocol_encoder_ford_v0_alloc(SubGhzEnvironment *environment)
{
    UNUSED(environment);
    SubGhzProtocolEncoderFordV0 *instance = malloc(sizeof(SubGhzProtocolEncoderFordV0));
    
    instance->base.protocol = &ford_protocol_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    
    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 2048;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    
    instance->key1 = 0;
    instance->key2 = 0;
    instance->serial = 0;
    instance->button = 0;
    instance->count = 0;
    
    FURI_LOG_I(TAG, "Encoder allocated");
    return instance;
}

void subghz_protocol_encoder_ford_v0_free(void *context)
{
    furi_assert(context);
    SubGhzProtocolEncoderFordV0 *instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

static void subghz_protocol_encoder_ford_v0_get_upload(SubGhzProtocolEncoderFordV0 *instance)
{
    furi_assert(instance);
    size_t index = 0;
    
    // Invert for transmission
    uint64_t tx_key1 = ~instance->key1;
    uint16_t tx_key2 = ~instance->key2;
    
    FURI_LOG_I(TAG, "Building upload: key1=0x%016llX, key2=0x%04X", instance->key1, instance->key2);
    FURI_LOG_I(TAG, "Inverted for TX: key1=0x%016llX, key2=0x%04X", tx_key1, tx_key2);
    
    uint32_t te_short = subghz_protocol_ford_v0_const.te_short;
    uint32_t te_long = subghz_protocol_ford_v0_const.te_long;
    
    #define ADD_LEVEL(lvl, dur) do { \
        if(index > 0 && level_duration_get_level(instance->encoder.upload[index-1]) == (lvl)) { \
            uint32_t prev = level_duration_get_duration(instance->encoder.upload[index-1]); \
            instance->encoder.upload[index-1] = level_duration_make((lvl), prev + (dur)); \
        } else { \
            instance->encoder.upload[index++] = level_duration_make((lvl), (dur)); \
        } \
    } while(0)
    
    for(uint8_t burst = 0; burst < FORD_V0_TOTAL_BURSTS; burst++) {
        // Preamble start
        ADD_LEVEL(true, te_short);
        ADD_LEVEL(false, te_long);
        
        // Preamble body
        for(int i = 0; i < FORD_V0_PREAMBLE_PAIRS; i++) {
            ADD_LEVEL(true, te_long);
            ADD_LEVEL(false, te_long);
        }
        
        // Preamble end
        ADD_LEVEL(true, te_short);
        
        // Gap
        ADD_LEVEL(false, FORD_V0_GAP_US);
        
        // First bit (bit 62) - determines initial waveform
        bool first_bit = (tx_key1 >> 62) & 1;
        if(first_bit) {
            // First bit is 1: produces LongLow
            ADD_LEVEL(true, te_long);
        } else {
            // First bit is 0: produces ShortLow + LongHigh
            ADD_LEVEL(true, te_short);
            ADD_LEVEL(false, te_long);
        }
        
        bool prev_bit = first_bit;
        
        // Encode remaining 62 bits of key1 (bits 61:0)
        for(int bit = 61; bit >= 0; bit--) {
            bool curr_bit = (tx_key1 >> bit) & 1;
            
            if(!prev_bit && !curr_bit) {
                // 0→0: ShortLow + ShortHigh → output 0
                ADD_LEVEL(true, te_short);
                ADD_LEVEL(false, te_short);
            } else if(!prev_bit && curr_bit) {
                // 0→1: LongLow → output 1
                ADD_LEVEL(true, te_long);
            } else if(prev_bit && !curr_bit) {
                // 1→0: LongHigh → output 0
                ADD_LEVEL(false, te_long);
            } else {
                // 1→1: ShortHigh + ShortLow → output 1
                ADD_LEVEL(false, te_short);
                ADD_LEVEL(true, te_short);
            }
            
            prev_bit = curr_bit;
        }
        
        // Encode 16 bits of key2 (continuing from prev_bit)
        for(int bit = 15; bit >= 0; bit--) {
            bool curr_bit = (tx_key2 >> bit) & 1;
            
            if(!prev_bit && !curr_bit) {
                ADD_LEVEL(true, te_short);
                ADD_LEVEL(false, te_short);
            } else if(!prev_bit && curr_bit) {
                ADD_LEVEL(true, te_long);
            } else if(prev_bit && !curr_bit) {
                ADD_LEVEL(false, te_long);
            } else {
                ADD_LEVEL(false, te_short);
                ADD_LEVEL(true, te_short);
            }
            
            prev_bit = curr_bit;
        }
        
        // Inter-burst gap
        if(burst < FORD_V0_TOTAL_BURSTS - 1) {
            ADD_LEVEL(false, te_long * 20);
        }
    }
    
    #undef ADD_LEVEL
    
    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
    
    FURI_LOG_I(TAG, "Upload built: %d bursts, size_upload=%zu", FORD_V0_TOTAL_BURSTS, index);
}

SubGhzProtocolStatus subghz_protocol_encoder_ford_v0_deserialize(void *context, FlipperFormat *flipper_format)
{
    furi_assert(context);
    SubGhzProtocolEncoderFordV0 *instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = 10;
    
    flipper_format_rewind(flipper_format);
    
    do {
        FuriString *temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            furi_string_free(temp_str);
            break;
        }
        
        if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
            FURI_LOG_E(TAG, "Wrong protocol: %s", furi_string_get_cstr(temp_str));
            furi_string_free(temp_str);
            break;
        }
        furi_string_free(temp_str);
        
        uint32_t bit_count_temp;
        if(!flipper_format_read_uint32(flipper_format, "Bit", &bit_count_temp, 1)) {
            FURI_LOG_E(TAG, "Missing Bit");
            break;
        }
        instance->generic.data_count_bit = 64;
        
        temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Key", temp_str)) {
            FURI_LOG_E(TAG, "Missing Key");
            furi_string_free(temp_str);
            break;
        }
        
        const char *key_str = furi_string_get_cstr(temp_str);
        uint64_t key = 0;
        size_t str_len = strlen(key_str);
        size_t hex_pos = 0;
        
        for(size_t i = 0; i < str_len && hex_pos < 16; i++) {
            char c = key_str[i];
            if(c == ' ') continue;
            
            uint8_t nibble;
            if(c >= '0' && c <= '9') nibble = c - '0';
            else if(c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
            else if(c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
            else break;
            
            key = (key << 4) | nibble;
            hex_pos++;
        }
        furi_string_free(temp_str);
        
        instance->key1 = key;
        instance->generic.data = key;
        FURI_LOG_I(TAG, "Parsed key1: 0x%016llX", instance->key1);
        
        if(!flipper_format_read_uint32(flipper_format, "Serial", &instance->serial, 1)) {
            instance->serial = 0;
        } else {
            FURI_LOG_I(TAG, "Read serial: 0x%08lX", instance->serial);
        }
        instance->generic.serial = instance->serial;
        
        uint32_t btn_temp;
        if(flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1)) {
            instance->button = (uint8_t)btn_temp;
            FURI_LOG_I(TAG, "Read button: 0x%02X", instance->button);
        } else {
            instance->button = 0;
        }
        instance->generic.btn = instance->button;
        
        if(!flipper_format_read_uint32(flipper_format, "Cnt", &instance->count, 1)) {
            instance->count = 0;
        } else {
            FURI_LOG_I(TAG, "Read counter: 0x%05lX", instance->count);
        }
        instance->generic.cnt = instance->count;
        
        uint32_t bs_temp = 0;
        flipper_format_read_uint32(flipper_format, "BS", &bs_temp, 1);
        
        uint32_t crc_temp = 0;
        flipper_format_read_uint32(flipper_format, "CRC", &crc_temp, 1);
        
        instance->key2 = ((bs_temp & 0xFF) << 8) | (crc_temp & 0xFF);
        FURI_LOG_I(TAG, "Reconstructed key2: 0x%04X (BS=0x%02X, CRC=0x%02X)", 
                   instance->key2, (uint8_t)bs_temp, (uint8_t)crc_temp);
        
        if(!flipper_format_read_uint32(flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1)) {
            instance->encoder.repeat = 10;
        }
        
        subghz_protocol_encoder_ford_v0_get_upload(instance);
        
        if(instance->encoder.size_upload == 0) {
            FURI_LOG_E(TAG, "Upload build failed");
            break;
        }
        
        instance->encoder.is_running = true;
        
        FURI_LOG_I(TAG, "Encoder initialized: repeat=%u, size_upload=%zu",
                   instance->encoder.repeat, instance->encoder.size_upload);
        
        ret = SubGhzProtocolStatusOk;
    } while(false);
    
    return ret;
}

void subghz_protocol_encoder_ford_v0_stop(void *context)
{
    furi_assert(context);
    SubGhzProtocolEncoderFordV0 *instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_ford_v0_yield(void *context)
{
    furi_assert(context);
    SubGhzProtocolEncoderFordV0 *instance = context;
    
    if(!instance->encoder.is_running || instance->encoder.repeat == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }
    
    LevelDuration ret = instance->encoder.upload[instance->encoder.front];
    
    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }
    
    return ret;
}

// ============================================================================
// DECODER IMPLEMENTATION
// ============================================================================

static void ford_v0_add_bit(SubGhzProtocolDecoderFordV0 *instance, bool bit)
{
    FURI_LOG_D(TAG, "Manchester output bit %d: %d (count=%d)", instance->bit_count, bit, instance->bit_count + 1);
    
    uint32_t low = (uint32_t)instance->data_low;
    instance->data_low = (instance->data_low << 1) | (bit ? 1 : 0);
    instance->data_high = (instance->data_high << 1) | ((low >> 31) & 1);
    instance->bit_count++;
}

static void decode_ford_v0(uint64_t key1, uint16_t key2, uint32_t *serial, uint8_t *button, uint32_t *count)
{
    uint8_t buf[13] = {0};

    for(int i = 0; i < 8; ++i)
        buf[i] = (uint8_t)(key1 >> (56 - i * 8));

    buf[8] = (uint8_t)(key2 >> 8);
    buf[9] = (uint8_t)(key2 & 0xFF);

    uint8_t tmp = buf[8];
    uint8_t parity = 0;
    uint8_t parity_any = (tmp != 0);
    while(tmp) {
        parity ^= (tmp & 1);
        tmp >>= 1;
    }
    buf[11] = parity_any ? parity : 0;

    uint8_t xor_byte;
    uint8_t limit;
    if(buf[11]) {
        xor_byte = buf[7];
        limit = 7;
    } else {
        xor_byte = buf[6];
        limit = 6;
    }

    for(int idx = 1; idx < limit; ++idx)
        buf[idx] ^= xor_byte;

    if(buf[11] == 0)
        buf[7] ^= xor_byte;

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
    if(instance->bit_count == 64) {
        uint64_t combined = ((uint64_t)instance->data_high << 32) | instance->data_low;
        instance->key1 = ~combined;
        instance->data_low = 0;
        instance->data_high = 0;
        return false;
    }

    if(instance->bit_count == 80) {
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

    switch(instance->decoder.parser_step) {
    case FordV0DecoderStepReset:
        if(level && (DURATION_DIFF(duration, te_short) < te_delta)) {
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
        if(!level) {
            if(DURATION_DIFF(duration, te_long) < te_delta) {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = FordV0DecoderStepPreambleCheck;
            } else {
                instance->decoder.parser_step = FordV0DecoderStepReset;
            }
        }
        break;

    case FordV0DecoderStepPreambleCheck:
        if(level) {
            if(DURATION_DIFF(duration, te_long) < te_delta) {
                instance->header_count++;
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = FordV0DecoderStepPreamble;
            } else if(DURATION_DIFF(duration, te_short) < te_delta) {
                instance->decoder.parser_step = FordV0DecoderStepGap;
            } else {
                instance->decoder.parser_step = FordV0DecoderStepReset;
            }
        }
        break;

    case FordV0DecoderStepGap:
        if(!level && (DURATION_DIFF(duration, gap_threshold) < 250)) {
            instance->data_low = 1;
            instance->data_high = 0;
            instance->bit_count = 1;
            instance->decoder.parser_step = FordV0DecoderStepData;
        } else if(!level && duration > gap_threshold + 250) {
            instance->decoder.parser_step = FordV0DecoderStepReset;
        }
        break;

    case FordV0DecoderStepData: {
        ManchesterEvent event;

        if(DURATION_DIFF(duration, te_short) < te_delta) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
            FURI_LOG_D(TAG, "Data: %s %luus -> %s", 
                    level ? "HIGH" : "LOW", 
                    (unsigned long)duration,
                    level ? "ShortLow" : "ShortHigh");
        } else if(DURATION_DIFF(duration, te_long) < te_delta) {
            event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
            FURI_LOG_D(TAG, "Data: %s %luus -> %s", 
                    level ? "HIGH" : "LOW", 
                    (unsigned long)duration,
                    level ? "LongLow" : "LongHigh");
        } else {
            FURI_LOG_D(TAG, "Data: %s %luus -> INVALID (reset)", 
                    level ? "HIGH" : "LOW", 
                    (unsigned long)duration);
            instance->decoder.parser_step = FordV0DecoderStepReset;
            break;
        }

        bool data_bit;
        if(manchester_advance(instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
            ford_v0_add_bit(instance, data_bit);

            if(ford_v0_process_data(instance)) {
                instance->generic.data = instance->key1;
                instance->generic.data_count_bit = 64;
                instance->generic.serial = instance->serial;
                instance->generic.btn = instance->button;
                instance->generic.cnt = instance->count;

                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);

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

    SubGhzProtocolStatus ret = subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t temp = (instance->key2 >> 8) & 0xFF;
        flipper_format_write_uint32(flipper_format, "BS", &temp, 1);

        temp = instance->key2 & 0xFF;
        flipper_format_write_uint32(flipper_format, "CRC", &temp, 1);

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

    const char* button_name = "??";
    if(instance->button == 0x01) button_name = "Close";
    else if(instance->button == 0x02) button_name = "Open";
    else if(instance->button == 0x04) button_name = "Trunk";

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key1:%08lX%08lX\r\n"
        "Key2:%04X\r\n"
        "Sn:%08lX Btn:%02X:%s\r\n"
        "Cnt:%05lX BS:%02X CRC:%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        instance->key2,
        instance->serial,
        instance->button,
        button_name,
        instance->count,
        (instance->key2 >> 8) & 0xFF,
        instance->key2 & 0xFF);
}