#include "vw.h"

#define TAG "VWProtocol"
#define VW_ENCODER_SYNC_COUNT    43
#define VW_ENCODER_MED_COUNT     4

static const SubGhzBlockConst subghz_protocol_vw_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 120,
    .min_count_bit_for_found = 80,
};

typedef struct SubGhzProtocolDecoderVw {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint64_t data_2;
} SubGhzProtocolDecoderVw;

typedef struct SubGhzProtocolEncoderVw {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint64_t data_2;
} SubGhzProtocolEncoderVw;

typedef enum {
    VwDecoderStepReset = 0,
    VwDecoderStepFoundSync,
    VwDecoderStepFoundStart1,
    VwDecoderStepFoundStart2,
    VwDecoderStepFoundStart3,
    VwDecoderStepFoundData,
} VwDecoderStep;

static bool vw_manchester_advance(
    ManchesterState state,
    ManchesterEvent event,
    ManchesterState* next_state,
    bool* data) {
    bool result = false;
    ManchesterState new_state = ManchesterStateMid1;

    if(event == ManchesterEventReset) {
        new_state = ManchesterStateMid1;
    } else if(state == ManchesterStateMid0 || state == ManchesterStateMid1) {
        if(event == ManchesterEventShortHigh) {
            new_state = ManchesterStateStart1;
        } else if(event == ManchesterEventShortLow) {
            new_state = ManchesterStateStart0;
        } else {
            new_state = ManchesterStateMid1;
        }
    } else if(state == ManchesterStateStart1) {
        if(event == ManchesterEventShortLow) {
            new_state = ManchesterStateMid1;
            result = true;
            if(data) *data = true;
        } else if(event == ManchesterEventLongLow) {
            new_state = ManchesterStateStart0;
            result = true;
            if(data) *data = true;
        } else {
            new_state = ManchesterStateMid1;
        }
    } else if(state == ManchesterStateStart0) {
        if(event == ManchesterEventShortHigh) {
            new_state = ManchesterStateMid0;
            result = true;
            if(data) *data = false;
        } else if(event == ManchesterEventLongHigh) {
            new_state = ManchesterStateStart1;
            result = true;
            if(data) *data = false;
        } else {
            new_state = ManchesterStateMid1;
        }
    }

    *next_state = new_state;
    return result;
}

static uint8_t vw_get_bit_index(uint8_t bit) {
    uint8_t bit_index = 0;

    if(bit < 72 && bit >= 8) {
        bit_index = bit - 8;
    } else {
        if(bit >= 72) {
            bit_index = bit - 64;
        }
        if(bit < 8) {
            bit_index = bit;
        }
        bit_index |= 0x80;
    }

    return bit_index;
}

static void vw_add_bit(SubGhzProtocolDecoderVw* instance, bool level) {
    if(instance->generic.data_count_bit >= subghz_protocol_vw_const.min_count_bit_for_found) {
        return;
    }

    uint8_t bit_index_full =
        subghz_protocol_vw_const.min_count_bit_for_found - 1 - instance->generic.data_count_bit;
    uint8_t bit_index_masked = vw_get_bit_index(bit_index_full);
    uint8_t bit_index = bit_index_masked & 0x7F;

    if(bit_index_masked & 0x80) {
        if(level) {
            instance->data_2 |= (1ULL << bit_index);
        } else {
            instance->data_2 &= ~(1ULL << bit_index);
        }
    } else {
        if(level) {
            instance->generic.data |= (1ULL << bit_index);
        } else {
            instance->generic.data &= ~(1ULL << bit_index);
        }
    }

    instance->generic.data_count_bit++;

    if(instance->generic.data_count_bit >= subghz_protocol_vw_const.min_count_bit_for_found) {
        if(instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }
    }
}

static bool vw_encoder_get_bit(SubGhzProtocolEncoderVw* instance, uint8_t bit_num) {
    uint8_t bit_index_full = subghz_protocol_vw_const.min_count_bit_for_found - 1 - bit_num;
    uint8_t bit_index_masked = vw_get_bit_index(bit_index_full);
    uint8_t bit_index = bit_index_masked & 0x7F;

    if(bit_index_masked & 0x80) {
        return (instance->data_2 >> bit_index) & 1;
    } else {
        return (instance->generic.data >> bit_index) & 1;
    }
}

static void subghz_protocol_encoder_vw_get_upload(SubGhzProtocolEncoderVw* instance) {
    uint32_t te_short = subghz_protocol_vw_const.te_short;
    uint32_t te_long = subghz_protocol_vw_const.te_long;
    uint32_t te_med = (te_long + te_short) / 2;

    size_t index = 0;

    for(uint8_t i = 0; i < VW_ENCODER_SYNC_COUNT; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, te_short);
        instance->encoder.upload[index++] = level_duration_make(false, te_short);
    }

    instance->encoder.upload[index++] = level_duration_make(true, te_long);

    instance->encoder.upload[index++] = level_duration_make(false, te_short);
    
    for(uint8_t i = 0; i < VW_ENCODER_MED_COUNT; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, te_med);
        instance->encoder.upload[index++] = level_duration_make(false, te_med);
    }

    instance->encoder.upload[index++] = level_duration_make(true, te_short);
    ManchesterState enc_state = ManchesterStateStart1;

    for(uint8_t bit_num = 0; bit_num < 80; bit_num++) {
        bool curr_bit = vw_encoder_get_bit(instance, bit_num);
        bool is_last = (bit_num == 79);
        bool next_bit = is_last ? curr_bit : vw_encoder_get_bit(instance, bit_num + 1);

        if(curr_bit) {
            if(enc_state == ManchesterStateStart1) {
                if(!is_last && !next_bit) {
                    instance->encoder.upload[index++] = level_duration_make(false, te_long);
                    enc_state = ManchesterStateStart0;
                } else {
                    instance->encoder.upload[index++] = level_duration_make(false, te_short);
                    enc_state = ManchesterStateMid1;
                }
            } else if(enc_state == ManchesterStateMid1 || enc_state == ManchesterStateMid0) {
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
                enc_state = ManchesterStateStart1;
                bit_num--;
            } else if(enc_state == ManchesterStateStart0) {
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
                enc_state = ManchesterStateMid0;
                bit_num--;
            }
        } else {
            if(enc_state == ManchesterStateStart0) {
                if(!is_last && next_bit) {
                    instance->encoder.upload[index++] = level_duration_make(true, te_long);
                    enc_state = ManchesterStateStart1;
                } else {
                    instance->encoder.upload[index++] = level_duration_make(true, te_short);
                    enc_state = ManchesterStateMid0;
                }
            } else if(enc_state == ManchesterStateMid1 || enc_state == ManchesterStateMid0) {
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
                enc_state = ManchesterStateStart0;
                bit_num--;
            } else if(enc_state == ManchesterStateStart1) {
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
                enc_state = ManchesterStateMid1;
                bit_num--;
            }
        }
    }

    if(index > 0) {
        LevelDuration* last = &instance->encoder.upload[index - 1];
        if(!last->level) {
            last->duration = te_long * 10;
        } else {
            instance->encoder.upload[index++] = level_duration_make(false, te_long * 10);
        }
    }

    instance->encoder.size_upload = index;
}

void* subghz_protocol_decoder_vw_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderVw* instance = malloc(sizeof(SubGhzProtocolDecoderVw));
    instance->base.protocol = &vw_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_vw_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderVw* instance = context;
    free(instance);
}

void subghz_protocol_decoder_vw_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderVw* instance = context;
    instance->decoder.parser_step = VwDecoderStepReset;
    instance->generic.data_count_bit = 0;
    instance->generic.data = 0;
    instance->data_2 = 0;
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_vw_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderVw* instance = context;

    uint32_t te_short = subghz_protocol_vw_const.te_short;
    uint32_t te_long = subghz_protocol_vw_const.te_long;
    uint32_t te_delta = subghz_protocol_vw_const.te_delta;
    uint32_t te_med = (te_long + te_short) / 2;
    uint32_t te_end = te_long * 5;

    ManchesterEvent event = ManchesterEventReset;

    switch(instance->decoder.parser_step) {
    case VwDecoderStepReset:
        if(DURATION_DIFF(duration, te_short) < te_delta) {
            instance->decoder.parser_step = VwDecoderStepFoundSync;
        }
        break;

    case VwDecoderStepFoundSync:
        if(DURATION_DIFF(duration, te_short) < te_delta) {
            break;
        }

        if(level && DURATION_DIFF(duration, te_long) < te_delta) {
            instance->decoder.parser_step = VwDecoderStepFoundStart1;
            break;
        }

        instance->decoder.parser_step = VwDecoderStepReset;
        break;

    case VwDecoderStepFoundStart1:
        if(!level && DURATION_DIFF(duration, te_short) < te_delta) {
            instance->decoder.parser_step = VwDecoderStepFoundStart2;
            break;
        }

        instance->decoder.parser_step = VwDecoderStepReset;
        break;

    case VwDecoderStepFoundStart2:
        if(level && DURATION_DIFF(duration, te_med) < te_delta) {
            instance->decoder.parser_step = VwDecoderStepFoundStart3;
            break;
        }

        instance->decoder.parser_step = VwDecoderStepReset;
        break;

    case VwDecoderStepFoundStart3:
        if(DURATION_DIFF(duration, te_med) < te_delta) {
            break;
        }

        if(level && DURATION_DIFF(duration, te_short) < te_delta) {
            vw_manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
            vw_manchester_advance(
                instance->manchester_state,
                ManchesterEventShortHigh,
                &instance->manchester_state,
                NULL);
            instance->generic.data_count_bit = 0;
            instance->generic.data = 0;
            instance->data_2 = 0;
            instance->decoder.parser_step = VwDecoderStepFoundData;
            break;
        }

        instance->decoder.parser_step = VwDecoderStepReset;
        break;

    case VwDecoderStepFoundData:
        if(DURATION_DIFF(duration, te_short) < te_delta) {
            event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
        }

        if(DURATION_DIFF(duration, te_long) < te_delta) {
            event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        }

        if(instance->generic.data_count_bit ==
               subghz_protocol_vw_const.min_count_bit_for_found - 1 &&
           !level && duration > te_end) {
            event = ManchesterEventShortLow;
        }

        if(event == ManchesterEventReset) {
            subghz_protocol_decoder_vw_reset(instance);
        } else {
            bool new_level;
            if(vw_manchester_advance(
                   instance->manchester_state, event, &instance->manchester_state, &new_level)) {
                vw_add_bit(instance, new_level);
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_vw_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderVw* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_vw_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderVw* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    ret = subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t type = (instance->data_2 >> 8) & 0xFF;
        uint32_t check = instance->data_2 & 0xFF;
        uint32_t btn = (check >> 4) & 0xF;

        flipper_format_write_uint32(flipper_format, "Type", &type, 1);
        flipper_format_write_uint32(flipper_format, "Check", &check, 1);
        flipper_format_write_uint32(flipper_format, "Btn", &btn, 1);
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_vw_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderVw* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_vw_const.min_count_bit_for_found);
}

static const char* vw_get_button_name(uint8_t btn) {
    switch(btn) {
    case 0x1:
        return "UNLOCK";
    case 0x2:
        return "LOCK";
    case 0x3:
        return "Un+Lk";
    case 0x4:
        return "TRUNK";
    case 0x5:
        return "Un+Tr";
    case 0x6:
        return "Lk+Tr";
    case 0x7:
        return "Un+Lk+Tr";
    case 0x8:
        return "PANIC";
    default:
        return "Unknown";
    }
}

void subghz_protocol_decoder_vw_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderVw* instance = context;

    uint8_t type = (instance->data_2 >> 8) & 0xFF;
    uint8_t check = instance->data_2 & 0xFF;
    uint8_t btn = (check >> 4) & 0xF;

    uint32_t key_high = (instance->generic.data >> 32) & 0xFFFFFFFF;
    uint32_t key_low = instance->generic.data & 0xFFFFFFFF;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%02X%08lX%08lX%02X\r\n"
        "Type:%02X Btn:%X %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        type,
        key_high,
        key_low,
        check,
        type,
        btn,
        vw_get_button_name(btn));
}

void* subghz_protocol_encoder_vw_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderVw* instance = malloc(sizeof(SubGhzProtocolEncoderVw));
    instance->base.protocol = &vw_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_vw_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderVw* instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

void subghz_protocol_encoder_vw_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderVw* instance = context;
    instance->encoder.is_running = false;
}

SubGhzProtocolStatus subghz_protocol_encoder_vw_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderVw* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic, flipper_format, subghz_protocol_vw_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) break;

        uint32_t type_temp = 0;
        uint32_t check_temp = 0;

        flipper_format_rewind(flipper_format);

        if(flipper_format_read_uint32(flipper_format, "Type", &type_temp, 1) &&
           flipper_format_read_uint32(flipper_format, "Check", &check_temp, 1)) {
            instance->data_2 = ((type_temp & 0xFF) << 8) | (check_temp & 0xFF);
        } else {
            instance->data_2 = 0;
        }

        size_t max_upload = 512;
        if(instance->encoder.upload) {
            free(instance->encoder.upload);
        }
        instance->encoder.upload = malloc(max_upload * sizeof(LevelDuration));
        if(!instance->encoder.upload) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        subghz_protocol_encoder_vw_get_upload(instance);

        instance->encoder.is_running = true;
        instance->encoder.repeat = 3;
        instance->encoder.front = 0;

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

LevelDuration subghz_protocol_encoder_vw_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderVw* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
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

const SubGhzProtocolDecoder subghz_protocol_vw_decoder = {
    .alloc = subghz_protocol_decoder_vw_alloc,
    .free = subghz_protocol_decoder_vw_free,
    .feed = subghz_protocol_decoder_vw_feed,
    .reset = subghz_protocol_decoder_vw_reset,
    .get_hash_data = subghz_protocol_decoder_vw_get_hash_data,
    .serialize = subghz_protocol_decoder_vw_serialize,
    .deserialize = subghz_protocol_decoder_vw_deserialize,
    .get_string = subghz_protocol_decoder_vw_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_vw_encoder = {
    .alloc = subghz_protocol_encoder_vw_alloc,
    .free = subghz_protocol_encoder_vw_free,
    .deserialize = subghz_protocol_encoder_vw_deserialize,
    .stop = subghz_protocol_encoder_vw_stop,
    .yield = subghz_protocol_encoder_vw_yield,
};

const SubGhzProtocol vw_protocol = {
    .name = VW_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_vw_decoder,
    .encoder = &subghz_protocol_vw_encoder,
};
