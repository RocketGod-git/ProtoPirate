// protopirate_history.c
#include "protopirate_history.h"
#include <lib/subghz/receiver.h>
#include <flipper_format/flipper_format_i.h>

#define TAG "ProtoPirateHistory"

typedef struct {
    FuriString* item_str;
    FlipperFormat* flipper_format;
    uint8_t type;
    SubGhzRadioPreset* preset;
} ProtoPirateHistoryItem;

ARRAY_DEF(ProtoPirateHistoryItemArray, ProtoPirateHistoryItem, M_POD_OPLIST)

struct ProtoPirateHistory {
    ProtoPirateHistoryItemArray_t data;
    uint16_t last_index;
    uint32_t last_update_timestamp;
    uint8_t code_last_hash_data;
};

ProtoPirateHistory* protopirate_history_alloc(void) {
    ProtoPirateHistory* instance = malloc(sizeof(ProtoPirateHistory));
    ProtoPirateHistoryItemArray_init(instance->data);
    instance->last_index = 0;
    return instance;
}

void protopirate_history_free(ProtoPirateHistory* instance) {
    furi_assert(instance);
    for(size_t i = 0; i < ProtoPirateHistoryItemArray_size(instance->data); i++) {
        ProtoPirateHistoryItem* item = ProtoPirateHistoryItemArray_get(instance->data, i);
        furi_string_free(item->item_str);
        flipper_format_free(item->flipper_format);
        free(item->preset);
    }
    ProtoPirateHistoryItemArray_clear(instance->data);
    free(instance);
}

void protopirate_history_reset(ProtoPirateHistory* instance) {
    furi_assert(instance);
    for(size_t i = 0; i < ProtoPirateHistoryItemArray_size(instance->data); i++) {
        ProtoPirateHistoryItem* item = ProtoPirateHistoryItemArray_get(instance->data, i);
        furi_string_free(item->item_str);
        flipper_format_free(item->flipper_format);
        free(item->preset);
    }
    ProtoPirateHistoryItemArray_reset(instance->data);
    instance->last_index = 0;
}

uint16_t protopirate_history_get_item(ProtoPirateHistory* instance) {
    furi_assert(instance);
    return ProtoPirateHistoryItemArray_size(instance->data);
}

uint16_t protopirate_history_get_last_index(ProtoPirateHistory* instance) {
    furi_assert(instance);
    return instance->last_index;
}

bool protopirate_history_add_to_history(
    ProtoPirateHistory* instance,
    void* context,
    SubGhzRadioPreset* preset) {
    furi_assert(instance);
    furi_assert(context);

    if(ProtoPirateHistoryItemArray_size(instance->data) >= KIA_HISTORY_MAX) {
        return false;
    }

    SubGhzProtocolDecoderBase* decoder_base = context;
    if((instance->code_last_hash_data ==
        subghz_protocol_decoder_base_get_hash_data(decoder_base)) &&
       ((furi_get_tick() - instance->last_update_timestamp) < 500)) {
        instance->last_update_timestamp = furi_get_tick();
        return false;
    }

    instance->code_last_hash_data = subghz_protocol_decoder_base_get_hash_data(decoder_base);
    instance->last_update_timestamp = furi_get_tick();

    // Create a new history item
    ProtoPirateHistoryItem* item = ProtoPirateHistoryItemArray_push_raw(instance->data);
    item->item_str = furi_string_alloc();
    item->flipper_format = flipper_format_string_alloc();
    item->type = 0;

    // Copy preset
    item->preset = malloc(sizeof(SubGhzRadioPreset));
    item->preset->frequency = preset->frequency;
    item->preset->name = furi_string_alloc();
    furi_string_set(item->preset->name, preset->name);
    item->preset->data = preset->data;
    item->preset->data_size = preset->data_size;

    // Get string representation
    FuriString* text = furi_string_alloc();
    subghz_protocol_decoder_base_get_string(decoder_base, text);
    furi_string_set(item->item_str, text);

    // Serialize to flipper format
    subghz_protocol_decoder_base_serialize(decoder_base, item->flipper_format, preset);

    furi_string_free(text);

    instance->last_index++;

    FURI_LOG_I(TAG, "Added item %u to history", instance->last_index);

    return true;
}

void protopirate_history_get_text_item_menu(
    ProtoPirateHistory* instance,
    FuriString* output,
    uint16_t idx) {
    furi_assert(instance);
    furi_assert(output);

    if(idx >= ProtoPirateHistoryItemArray_size(instance->data)) {
        furi_string_set(output, "---");
        return;
    }

    ProtoPirateHistoryItem* item = ProtoPirateHistoryItemArray_get(instance->data, idx);

    // Get just the first line for the menu
    const char* str = furi_string_get_cstr(item->item_str);
    const char* newline = strchr(str, '\r');
    if(newline) {
        size_t len = newline - str;
        furi_string_set_strn(output, str, len);
    } else {
        newline = strchr(str, '\n');
        if(newline) {
            size_t len = newline - str;
            furi_string_set_strn(output, str, len);
        } else {
            furi_string_set(output, item->item_str);
        }
    }
}

void protopirate_history_get_text_item(
    ProtoPirateHistory* instance,
    FuriString* output,
    uint16_t idx) {
    furi_assert(instance);
    furi_assert(output);

    if(idx >= ProtoPirateHistoryItemArray_size(instance->data)) {
        furi_string_set(output, "---");
        return;
    }

    ProtoPirateHistoryItem* item = ProtoPirateHistoryItemArray_get(instance->data, idx);
    furi_string_set(output, item->item_str);
}

SubGhzProtocolDecoderBase*
    protopirate_history_get_decoder_base(ProtoPirateHistory* instance, uint16_t idx) {
    UNUSED(instance);
    UNUSED(idx);
    // This would need the environment to recreate the decoder
    // For now, return NULL - use get_text_item instead
    return NULL;
}

FlipperFormat* protopirate_history_get_raw_data(ProtoPirateHistory* instance, uint16_t idx) {
    furi_assert(instance);

    if(idx >= ProtoPirateHistoryItemArray_size(instance->data)) {
        return NULL;
    }

    ProtoPirateHistoryItem* item = ProtoPirateHistoryItemArray_get(instance->data, idx);
    return item->flipper_format;
}
