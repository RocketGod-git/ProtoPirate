// scenes/protopirate_scene_receiver.c
#include "../protopirate_app_i.h"
#include <notification/notification_messages.h>

#define TAG                     "ProtoPirateSceneRx"
#define KIA_DISPLAY_HISTORY_MAX 50

// Forward declaration
void protopirate_scene_receiver_view_callback(ProtoPirateCustomEvent event, void* context);

static void protopirate_scene_receiver_update_statusbar(void* context) {
    ProtoPirateApp* app = context;
    FuriString* frequency_str = furi_string_alloc();
    FuriString* modulation_str = furi_string_alloc();
    FuriString* history_stat_str = furi_string_alloc();

    protopirate_get_frequency_modulation(app, frequency_str, modulation_str);

    furi_string_printf(
        history_stat_str,
        "%u/%u",
        protopirate_history_get_item(app->txrx->history),
        KIA_DISPLAY_HISTORY_MAX);

    protopirate_view_receiver_add_data_statusbar(
        app->protopirate_receiver,
        furi_string_get_cstr(frequency_str),
        furi_string_get_cstr(modulation_str),
        furi_string_get_cstr(history_stat_str),
        false);

    furi_string_free(frequency_str);
    furi_string_free(modulation_str);
    furi_string_free(history_stat_str);
}

static void protopirate_scene_receiver_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    UNUSED(receiver);
    furi_assert(context);
    ProtoPirateApp* app = context;

    FURI_LOG_I(TAG, "=== SIGNAL DECODED ===");

    FuriString* str_buff = furi_string_alloc();
    subghz_protocol_decoder_base_get_string(decoder_base, str_buff);
    FURI_LOG_I(TAG, "%s", furi_string_get_cstr(str_buff));

    // Add to history
    if(protopirate_history_add_to_history(app->txrx->history, decoder_base, app->txrx->preset)) {
        notification_message(app->notifications, &sequence_semi_success);

        FURI_LOG_I(
            TAG,
            "Added to history, total items: %u",
            protopirate_history_get_item(app->txrx->history));

        FuriString* item_name = furi_string_alloc();
        protopirate_history_get_text_item_menu(
            app->txrx->history, item_name, protopirate_history_get_item(app->txrx->history) - 1);

        protopirate_view_receiver_add_item_to_menu(
            app->protopirate_receiver, furi_string_get_cstr(item_name), 0);

        furi_string_free(item_name);

        view_dispatcher_send_custom_event(
            app->view_dispatcher, ProtoPirateCustomEventSceneReceiverUpdate);
    } else {
        FURI_LOG_W(TAG, "Failed to add to history (duplicate or full)");
    }

    furi_string_free(str_buff);

    // Pause hopper when we receive something
    if(app->txrx->hopper_state == ProtoPirateHopperStateRunning) {
        app->txrx->hopper_state = ProtoPirateHopperStatePause;
        app->txrx->hopper_timeout = 10;
    }
}

void protopirate_scene_receiver_on_enter(void* context) {
    ProtoPirateApp* app = context;

    FURI_LOG_I(TAG, "=== ENTERING RECEIVER SCENE ===");
    FURI_LOG_I(TAG, "Frequency: %lu Hz", app->txrx->preset->frequency);
    FURI_LOG_I(TAG, "Modulation: %s", furi_string_get_cstr(app->txrx->preset->name));

    // Set up the receiver callback
    subghz_receiver_set_rx_callback(app->txrx->receiver, protopirate_scene_receiver_callback, app);

    // Set up view callback
    protopirate_view_receiver_set_callback(
        app->protopirate_receiver, protopirate_scene_receiver_view_callback, app);

    // Update status bar
    protopirate_scene_receiver_update_statusbar(app);

    // Start hopper if enabled
    if(app->txrx->hopper_state != ProtoPirateHopperStateOFF) {
        app->txrx->hopper_state = ProtoPirateHopperStateRunning;
    }

    // Get preset data
    const char* preset_name = furi_string_get_cstr(app->txrx->preset->name);
    uint8_t* preset_data = subghz_setting_get_preset_data_by_name(app->setting, preset_name);

    if(preset_data == NULL) {
        FURI_LOG_E(TAG, "Failed to get preset data for %s, using AM650", preset_name);
        preset_data = subghz_setting_get_preset_data_by_name(app->setting, "AM650");
    }

    // Begin receiving
    protopirate_begin(app, preset_data);

    uint32_t frequency = app->txrx->preset->frequency;
    if(app->txrx->hopper_state == ProtoPirateHopperStateRunning) {
        frequency = subghz_setting_get_hopper_frequency(app->setting, 0);
        app->txrx->hopper_idx_frequency = 0;
    }

    FURI_LOG_I(TAG, "Starting RX on %lu Hz", frequency);
    protopirate_rx(app, frequency);
    FURI_LOG_I(TAG, "RX started, state: %d", app->txrx->txrx_state);

    // Switch to receiver view
    view_dispatcher_switch_to_view(app->view_dispatcher, ProtoPirateViewReceiver);
}

bool protopirate_scene_receiver_on_event(void* context, SceneManagerEvent event) {
    ProtoPirateApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case ProtoPirateCustomEventSceneReceiverUpdate:
            protopirate_scene_receiver_update_statusbar(app);
            consumed = true;
            break;

        case ProtoPirateCustomEventViewReceiverOK: {
            uint16_t idx = protopirate_view_receiver_get_idx_menu(app->protopirate_receiver);
            FURI_LOG_I(TAG, "Selected item %d", idx);
            if(idx < protopirate_history_get_item(app->txrx->history)) {
                app->txrx->idx_menu_chosen = idx;
                scene_manager_next_scene(app->scene_manager, ProtoPirateSceneReceiverInfo);
            }
        }
            consumed = true;
            break;

        case ProtoPirateCustomEventViewReceiverConfig:
            scene_manager_next_scene(app->scene_manager, ProtoPirateSceneReceiverConfig);
            consumed = true;
            break;

        case ProtoPirateCustomEventViewReceiverBack:
            if(app->txrx->txrx_state == ProtoPirateTxRxStateRx) {
                protopirate_rx_end(app);
            }
            protopirate_sleep(app);
            protopirate_history_reset(app->txrx->history);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, ProtoPirateSceneStart);
            consumed = true;
            break;

        case ProtoPirateCustomEventViewReceiverUnlock:
            consumed = true;
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        // Update hopper
        if(app->txrx->hopper_state != ProtoPirateHopperStateOFF) {
            protopirate_hopper_update(app);
            protopirate_scene_receiver_update_statusbar(app);
        }

        // Update RSSI
        if(app->txrx->txrx_state == ProtoPirateTxRxStateRx) {
            float rssi = subghz_devices_get_rssi(app->txrx->radio_device);
            protopirate_view_receiver_set_rssi(app->protopirate_receiver, rssi);
        }

        consumed = true;
    }

    return consumed;
}

void protopirate_scene_receiver_on_exit(void* context) {
    ProtoPirateApp* app = context;

    FURI_LOG_I(TAG, "=== EXITING RECEIVER SCENE ===");

    if(app->txrx->txrx_state == ProtoPirateTxRxStateRx) {
        protopirate_rx_end(app);
    }
}

void protopirate_scene_receiver_view_callback(ProtoPirateCustomEvent event, void* context) {
    furi_assert(context);
    ProtoPirateApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}
