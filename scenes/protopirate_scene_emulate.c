// scenes/protopirate_scene_emulate.c
#include "../protopirate_app_i.h"
#include "../helpers/protopirate_storage.h"
#include "../protocols/protocol_items.h"

#define TAG "ProtoPirateEmulate"

typedef struct
{
    uint32_t original_counter;
    uint32_t current_counter;
    uint32_t serial;
    uint8_t original_button;
    FuriString *protocol_name;
    FlipperFormat *flipper_format;
    SubGhzTransmitter *transmitter;
    bool is_transmitting;
} EmulateContext;

static EmulateContext *emulate_context = NULL;

static uint8_t protopirate_get_button_for_protocol(const char *protocol, InputKey key, uint8_t original)
{
    // Map D-pad to common car buttons
    // Protocol-specific mappings

    if (strstr(protocol, "Ford") || strstr(protocol, "Kia") || strstr(protocol, "Subaru"))
    {
        switch (key)
        {
        case InputKeyUp:
            return 0x1; // Unlock
        case InputKeyDown:
            return 0x2; // Lock
        case InputKeyLeft:
            return 0x8; // Trunk
        case InputKeyRight:
            return 0x4; // Panic
        case InputKeyOk:
            return original; // Original button
        default:
            return original;
        }
    }
    else if (strstr(protocol, "Suzuki"))
    {
        switch (key)
        {
        case InputKeyUp:
            return 0x4; // Unlock
        case InputKeyDown:
            return 0x3; // Lock
        case InputKeyLeft:
            return 0x2; // Trunk
        case InputKeyRight:
            return 0x1; // Panic
        case InputKeyOk:
            return original;
        default:
            return original;
        }
    }
    else if (strstr(protocol, "VW"))
    {
        switch (key)
        {
        case InputKeyUp:
            return 0x1; // Unlock
        case InputKeyDown:
            return 0x2; // Lock
        case InputKeyLeft:
            return 0x4; // Trunk
        case InputKeyRight:
            return 0x8; // Panic
        case InputKeyOk:
            return original;
        default:
            return original;
        }
    }

    return original;
}

static bool protopirate_emulate_update_data(EmulateContext *ctx, uint8_t button)
{
    if (!ctx || !ctx->flipper_format)
        return false;

    // Update button and counter in the flipper format
    flipper_format_rewind(ctx->flipper_format);

    // Update button
    uint32_t btn_value = button;
    if (!flipper_format_update_uint32(ctx->flipper_format, "Btn", &btn_value, 1))
    {
        // Try to insert if update failed
        flipper_format_insert_or_update_uint32(ctx->flipper_format, "Btn", &btn_value, 1);
    }

    // Update counter
    if (!flipper_format_update_uint32(ctx->flipper_format, "Cnt", &ctx->current_counter, 1))
    {
        flipper_format_insert_or_update_uint32(ctx->flipper_format, "Cnt", &ctx->current_counter, 1);
    }

    // For protocols with complex encoding, we need to regenerate the Key data
    const char *proto_name = furi_string_get_cstr(ctx->protocol_name);
    const SubGhzProtocol *protocol = NULL;
    for (size_t i = 0; i < protopirate_protocol_registry.size; i++)
    {
        if (strcmp(protopirate_protocol_registry.items[i]->name, proto_name) == 0)
        {
            protocol = protopirate_protocol_registry.items[i];
            break;
        }
    }

    // If the protocol has an encoder, re-deserialize the updated data into the transmitter
    // This will prepare the transmitter to encode the new data upon yielding
    if (protocol && protocol->encoder && protocol->encoder->deserialize && ctx->transmitter)
    {
        flipper_format_rewind(ctx->flipper_format);
        if(!subghz_transmitter_deserialize(ctx->transmitter, ctx->flipper_format)) {
            FURI_LOG_E(TAG, "Failed to deserialize for re-encoding");
            return false;
        }
    }

    return true;
}

static void protopirate_emulate_draw_callback(Canvas *canvas, void *context)
{
    UNUSED(context); // We're using the global emulate_context

    if (!emulate_context)
        return;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    // Title
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignTop,
                            furi_string_get_cstr(emulate_context->protocol_name));

    canvas_set_font(canvas, FontSecondary);

    // Serial
    char serial_str[32];
    snprintf(serial_str, sizeof(serial_str), "Serial: %08lX", emulate_context->serial);
    canvas_draw_str(canvas, 2, 20, serial_str);

    // Counter
    char counter_str[32];
    snprintf(counter_str, sizeof(counter_str), "Counter: %04lX -> %04lX",
             emulate_context->original_counter, emulate_context->current_counter);
    canvas_draw_str(canvas, 2, 32, counter_str);

    // Button mapping guide
    canvas_draw_str(canvas, 2, 44, "Up:Unlock Dn:Lock");
    canvas_draw_str(canvas, 2, 54, "L:Trunk R:Panic OK:Orig");

    // Transmitting indicator
    if (emulate_context->is_transmitting)
    {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "TRANSMITTING");
    }
}

static bool protopirate_emulate_input_callback(InputEvent *event, void *context)
{
    ProtoPirateApp *app = context;
    EmulateContext *ctx = emulate_context;

    if (!ctx)
        return false;

    if (event->type == InputTypePress || event->type == InputTypeRepeat)
    {
        if (event->key == InputKeyBack)
        {
            // Exit emulation
            view_dispatcher_send_custom_event(
                app->view_dispatcher, ProtoPirateCustomEventEmulateExit);
            return true;
        }

        // Get button mapping for this key
        uint8_t button = protopirate_get_button_for_protocol(
            furi_string_get_cstr(ctx->protocol_name),
            event->key,
            ctx->original_button);

        // Update data with new button and counter
        ctx->current_counter++;
        protopirate_emulate_update_data(ctx, button);

        // Transmit the signal
        ctx->is_transmitting = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, ProtoPirateCustomEventEmulateTransmit);

        return true;
    }
    else if (event->type == InputTypeRelease)
    {
        ctx->is_transmitting = false;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, ProtoPirateCustomEventEmulateStop);
        return true;
    }

    return false;
}

void protopirate_scene_emulate_on_enter(void *context)
{
    ProtoPirateApp *app = context;

    // Create emulate context
    emulate_context = malloc(sizeof(EmulateContext));
    memset(emulate_context, 0, sizeof(EmulateContext));

    emulate_context->protocol_name = furi_string_alloc();

    // Load the file
    if (app->loaded_file_path)
    {
        FlipperFormat *ff = protopirate_storage_load_file(
            furi_string_get_cstr(app->loaded_file_path));

        if (ff)
        {
            emulate_context->flipper_format = ff;

            // Read protocol name
            flipper_format_rewind(ff);
            flipper_format_read_string(ff, "Protocol", emulate_context->protocol_name);

            // Read serial
            flipper_format_rewind(ff);
            flipper_format_read_uint32(ff, "Serial", &emulate_context->serial, 1);

            // Read original button
            flipper_format_rewind(ff);
            uint32_t btn_temp;
            if (flipper_format_read_uint32(ff, "Btn", &btn_temp, 1))
            {
                emulate_context->original_button = (uint8_t)btn_temp;
            }

            // Read counter
            flipper_format_rewind(ff);
            if (flipper_format_read_uint32(ff, "Cnt", &emulate_context->original_counter, 1))
            {
                emulate_context->current_counter = emulate_context->original_counter;
            }

            // Set up transmitter based on protocol
            const char *proto_name = furi_string_get_cstr(emulate_context->protocol_name);

            // Find the protocol in the registry
            const SubGhzProtocol *protocol = NULL;
            for (size_t i = 0; i < protopirate_protocol_registry.size; i++)
            {
                if (strcmp(protopirate_protocol_registry.items[i]->name, proto_name) == 0)
                {
                    protocol = protopirate_protocol_registry.items[i];
                    break;
                }
            }

            if (protocol && protocol->encoder && protocol->encoder->alloc)
            {
                // Protocol has encoder support
                emulate_context->transmitter = subghz_transmitter_alloc_init(
                    app->txrx->environment, proto_name);

                if (emulate_context->transmitter)
                {
                    // Deserialize for transmission
                    flipper_format_rewind(ff);
                    subghz_transmitter_deserialize(emulate_context->transmitter, ff);
                }
            }
        }
    }

    // Set up view - pass emulate_context as the context, not app
    view_set_draw_callback(app->view_about, protopirate_emulate_draw_callback);
    view_set_input_callback(app->view_about, protopirate_emulate_input_callback);
    view_set_context(app->view_about, app); // Keep app for input callback
    view_set_previous_callback(app->view_about, NULL);

    view_dispatcher_switch_to_view(app->view_dispatcher, ProtoPirateViewAbout);
}

bool protopirate_scene_emulate_on_event(void *context, SceneManagerEvent event)
{
    ProtoPirateApp *app = context;
    bool consumed = false;

    if (event.type == SceneManagerEventTypeCustom)
    {
        switch (event.event)
        {
        case ProtoPirateCustomEventEmulateTransmit:
            if (emulate_context && emulate_context->transmitter)
            {
                // Start transmission
                if (app->txrx->txrx_state != ProtoPirateTxRxStateIDLE)
                {
                    protopirate_idle(app);
                }

                // Set frequency and preset
                uint32_t frequency;
                if (flipper_format_read_uint32(
                        emulate_context->flipper_format, "Frequency", &frequency, 1))
                {
                    protopirate_tx(app, frequency);

                    // Actually transmit
                    subghz_transmitter_yield(emulate_context->transmitter);

                    // Visual/haptic feedback
                    notification_message(app->notifications, &sequence_blink_start_cyan);
                    notification_message(app->notifications, &sequence_single_vibro);
                }
            }
            else
            {
                // No encoder - just show we would transmit
                notification_message(app->notifications, &sequence_error);
            }
            consumed = true;
            break;

        case ProtoPirateCustomEventEmulateStop:
            // Stop transmission
            if (app->txrx->txrx_state != ProtoPirateTxRxStateIDLE)
            {
                protopirate_idle(app);
            }
            notification_message(app->notifications, &sequence_blink_stop);
            consumed = true;
            break;

        case ProtoPirateCustomEventEmulateExit:
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
            break;
        }
    }
    else if (event.type == SceneManagerEventTypeTick)
    {
        // Update display
        view_commit_model(app->view_about, false);
        consumed = true;
    }

    return consumed;
}

void protopirate_scene_emulate_on_exit(void *context)
{
    ProtoPirateApp *app = context;

    // Clean up
    if (emulate_context)
    {
        if (emulate_context->transmitter)
        {
            subghz_transmitter_free(emulate_context->transmitter);
        }
        if (emulate_context->flipper_format)
        {
            flipper_format_free(emulate_context->flipper_format);
        }
        furi_string_free(emulate_context->protocol_name);
        free(emulate_context);
        emulate_context = NULL;
    }

    // Stop any ongoing transmission
    if (app->txrx->txrx_state != ProtoPirateTxRxStateIDLE)
    {
        protopirate_idle(app);
    }

    notification_message(app->notifications, &sequence_blink_stop);

    view_set_draw_callback(app->view_about, NULL);
    view_set_input_callback(app->view_about, NULL);
}