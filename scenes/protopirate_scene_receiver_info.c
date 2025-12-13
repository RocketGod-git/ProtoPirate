// scenes/protopirate_scene_receiver_info.c
#include "../protopirate_app_i.h"
#include "../helpers/protopirate_storage.h"

static void protopirate_scene_receiver_info_widget_callback(
    GuiButtonType result,
    InputType type,
    void *context)
{
    ProtoPirateApp *app = context;
    if (type == InputTypeShort)
    {
        if (result == GuiButtonTypeRight)
        {
            view_dispatcher_send_custom_event(
                app->view_dispatcher, ProtoPirateCustomEventReceiverInfoSave);
        }
    }
}

void protopirate_scene_receiver_info_on_enter(void *context)
{
    furi_assert(context);
    ProtoPirateApp *app = context;

    FuriString *text;
    text = furi_string_alloc();

    protopirate_history_get_text_item_menu(app->txrx->history, text, app->txrx->idx_menu_chosen);

    widget_add_string_element(
        app->widget, 64, 0, AlignCenter, AlignTop, FontPrimary, furi_string_get_cstr(text));

    furi_string_reset(text);
    protopirate_history_get_text_item(app->txrx->history, text, app->txrx->idx_menu_chosen);

    widget_add_string_multiline_element(
        app->widget, 0, 14, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(text));

    // Add save button
    widget_add_button_element(
        app->widget,
        GuiButtonTypeRight,
        "Save",
        protopirate_scene_receiver_info_widget_callback,
        app);

    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, ProtoPirateViewWidget);
}

bool protopirate_scene_receiver_info_on_event(void *context, SceneManagerEvent event)
{
    ProtoPirateApp *app = context;
    bool consumed = false;

    if (event.type == SceneManagerEventTypeCustom)
    {
        if (event.event == ProtoPirateCustomEventReceiverInfoSave)
        {
            // Get the flipper format from history
            FlipperFormat *ff = protopirate_history_get_raw_data(
                app->txrx->history, app->txrx->idx_menu_chosen);

            if (ff)
            {
                // Extract protocol name
                FuriString *protocol = furi_string_alloc();
                flipper_format_rewind(ff);
                if (!flipper_format_read_string(ff, "Protocol", protocol))
                {
                    furi_string_set_str(protocol, "Unknown");
                }

                FuriString *saved_path = furi_string_alloc();
                if (protopirate_storage_save_capture(
                        ff, furi_string_get_cstr(protocol), saved_path))
                {

                    // Show success notification
                    notification_message(app->notifications, &sequence_success);
                }
                else
                {
                    notification_message(app->notifications, &sequence_error);
                }

                furi_string_free(protocol);
                furi_string_free(saved_path);
            }
            consumed = true;
        }
    }

    return consumed;
}

void protopirate_scene_receiver_info_on_exit(void *context)
{
    furi_assert(context);
    ProtoPirateApp *app = context;
    widget_reset(app->widget);
}