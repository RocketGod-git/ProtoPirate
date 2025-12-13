// views/protopirate_receiver.c
#include "protopirate_receiver.h"
#include "../protopirate_app_i.h"
#include <input/input.h>
#include <gui/elements.h>
#include <furi.h>

#define FRAME_HEIGHT 12
#define MAX_LEN_PX   112
#define MENU_ITEMS   4u
#define UNLOCK_CNT   3

typedef struct {
    FuriString* item_str;
    uint8_t type;
} ProtoPirateReceiverMenuItem;

ARRAY_DEF(ProtoPirateReceiverMenuItemArray, ProtoPirateReceiverMenuItem, M_POD_OPLIST)

struct ProtoPirateReceiver {
    View* view;
    ProtoPirateReceiverCallback callback;
    void* context;
};

typedef struct {
    ProtoPirateReceiverMenuItemArray_t history_item_arr;
    uint8_t list_offset;
    uint8_t history_item;
    float rssi;
    FuriString* frequency_str;
    FuriString* preset_str;
    FuriString* history_stat_str;
    bool external_radio;
    ProtoPirateLock lock;
    uint8_t lock_count;
} ProtoPirateReceiverModel;

void protopirate_view_receiver_set_rssi(ProtoPirateReceiver* receiver, float rssi) {
    furi_assert(receiver);
    with_view_model(
        receiver->view, ProtoPirateReceiverModel * model, { model->rssi = rssi; }, true);
}

void protopirate_view_receiver_set_lock(ProtoPirateReceiver* receiver, ProtoPirateLock lock) {
    furi_assert(receiver);
    with_view_model(
        receiver->view, ProtoPirateReceiverModel * model, { model->lock = lock; }, true);
}

void protopirate_view_receiver_set_callback(
    ProtoPirateReceiver* receiver,
    ProtoPirateReceiverCallback callback,
    void* context) {
    furi_assert(receiver);
    receiver->callback = callback;
    receiver->context = context;
}

static void protopirate_view_receiver_update_offset(ProtoPirateReceiver* receiver) {
    furi_assert(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateReceiverModel * model,
        {
            size_t history_item = model->history_item;
            size_t list_offset = model->list_offset;
            size_t item_count = ProtoPirateReceiverMenuItemArray_size(model->history_item_arr);

            if(history_item < list_offset) {
                model->list_offset = history_item;
            } else if(history_item >= (list_offset + MENU_ITEMS)) {
                model->list_offset = history_item - (MENU_ITEMS - 1);
            }

            if(item_count < MENU_ITEMS) {
                model->list_offset = 0;
            } else if(model->list_offset > (item_count - MENU_ITEMS)) {
                model->list_offset = item_count - MENU_ITEMS;
            }
        },
        true);
}

void protopirate_view_receiver_add_item_to_menu(
    ProtoPirateReceiver* receiver,
    const char* name,
    uint8_t type) {
    furi_assert(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateReceiverModel * model,
        {
            ProtoPirateReceiverMenuItem* item_menu =
                ProtoPirateReceiverMenuItemArray_push_raw(model->history_item_arr);
            item_menu->item_str = furi_string_alloc_set(name);
            item_menu->type = type;
        },
        true);
    protopirate_view_receiver_update_offset(receiver);
}

void protopirate_view_receiver_add_data_statusbar(
    ProtoPirateReceiver* receiver,
    const char* frequency_str,
    const char* preset_str,
    const char* history_stat_str,
    bool external_radio) {
    furi_assert(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateReceiverModel * model,
        {
            furi_string_set_str(model->frequency_str, frequency_str);
            furi_string_set_str(model->preset_str, preset_str);
            furi_string_set_str(model->history_stat_str, history_stat_str);
            model->external_radio = external_radio;
        },
        true);
}

static void protopirate_view_receiver_draw_frame(Canvas* canvas, uint16_t idx, bool scrollbar) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0 + idx * FRAME_HEIGHT, scrollbar ? 122 : 127, FRAME_HEIGHT);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_dot(canvas, 0, 0 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, 1, 0 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, 0, (0 + idx * FRAME_HEIGHT) + 1);

    canvas_draw_dot(canvas, 0, (0 + idx * FRAME_HEIGHT) + 11);
    canvas_draw_dot(canvas, scrollbar ? 121 : 126, 0 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, scrollbar ? 121 : 126, (0 + idx * FRAME_HEIGHT) + 11);
}

static void protopirate_view_receiver_draw_rssi(Canvas* canvas, ProtoPirateReceiverModel* model) {
    uint8_t x = 58;
    uint8_t y = 51;
    float rssi = model->rssi;

    if(rssi >= -60.0f) {
        canvas_draw_box(canvas, x + 8, y + 2, 4, 5);
    }
    if(rssi >= -70.0f) {
        canvas_draw_box(canvas, x + 4, y + 4, 4, 3);
    }
    if(rssi >= -80.0f) {
        canvas_draw_box(canvas, x, y + 6, 4, 1);
    }
}

void protopirate_view_receiver_draw(Canvas* canvas, ProtoPirateReceiverModel* model) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    size_t item_count = ProtoPirateReceiverMenuItemArray_size(model->history_item_arr);
    bool scrollbar = item_count > MENU_ITEMS;

    FuriString* str_buff;
    str_buff = furi_string_alloc();

    if(item_count > 0) {
        size_t shift_position = model->list_offset;

        for(size_t i = 0; i < MIN(item_count, MENU_ITEMS); i++) {
            size_t idx = shift_position + i;
            ProtoPirateReceiverMenuItem* item =
                ProtoPirateReceiverMenuItemArray_get(model->history_item_arr, idx);

            furi_string_set(str_buff, item->item_str);
            elements_string_fit_width(canvas, str_buff, scrollbar ? MAX_LEN_PX - 6 : MAX_LEN_PX);

            if(model->history_item == idx) {
                protopirate_view_receiver_draw_frame(canvas, i, scrollbar);
            } else {
                canvas_set_color(canvas, ColorBlack);
            }

            canvas_draw_str(canvas, 4, 9 + (i * FRAME_HEIGHT), furi_string_get_cstr(str_buff));
        }

        if(scrollbar) {
            elements_scrollbar_pos(canvas, 128, 0, 49, shift_position, item_count);
        }
    } else {
        canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignCenter, "ProtoPirate");
        canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, "Waiting for signal...");
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "<- to Config");
    }

    furi_string_free(str_buff);

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_line(canvas, 0, 48, 127, 48);

    // Draw status bar
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 58, furi_string_get_cstr(model->frequency_str));
    canvas_draw_str(canvas, 40, 58, furi_string_get_cstr(model->preset_str));
    canvas_draw_str_aligned(
        canvas, 110, 58, AlignCenter, AlignBottom, furi_string_get_cstr(model->history_stat_str));

    // Draw RSSI
    protopirate_view_receiver_draw_rssi(canvas, model);

    // Draw external radio indicator if needed
    if(model->external_radio) {
        canvas_draw_str(canvas, 100, 58, "Ext");
    }

    // Draw lock indicator
    if(model->lock == ProtoPirateLockOn) {
        canvas_draw_str(canvas, 119, 58, "L");
    }
}

bool protopirate_view_receiver_input(InputEvent* event, void* context) {
    furi_assert(context);
    ProtoPirateReceiver* receiver = context;

    bool consumed = false;

    ProtoPirateLock lock;
    with_view_model(
        receiver->view, ProtoPirateReceiverModel * model, { lock = model->lock; }, false);

    if(lock == ProtoPirateLockOn) {
        with_view_model(
            receiver->view,
            ProtoPirateReceiverModel * model,
            {
                if(event->type == InputTypeShort && event->key == InputKeyBack) {
                    model->lock_count++;
                    if(model->lock_count >= UNLOCK_CNT) {
                        model->lock = ProtoPirateLockOff;
                        model->lock_count = 0;
                        if(receiver->callback) {
                            receiver->callback(
                                ProtoPirateCustomEventViewReceiverUnlock, receiver->context);
                        }
                    }
                } else {
                    model->lock_count = 0;
                }
            },
            true);
        consumed = true;
    } else if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyUp:
            with_view_model(
                receiver->view,
                ProtoPirateReceiverModel * model,
                {
                    if(model->history_item > 0) {
                        model->history_item--;
                    }
                },
                true);
            protopirate_view_receiver_update_offset(receiver);
            consumed = true;
            break;
        case InputKeyDown:
            with_view_model(
                receiver->view,
                ProtoPirateReceiverModel * model,
                {
                    size_t item_count =
                        ProtoPirateReceiverMenuItemArray_size(model->history_item_arr);
                    if(item_count > 0 && model->history_item < item_count - 1) {
                        model->history_item++;
                    }
                },
                true);
            protopirate_view_receiver_update_offset(receiver);
            consumed = true;
            break;
        case InputKeyLeft:
            if(receiver->callback) {
                receiver->callback(ProtoPirateCustomEventViewReceiverConfig, receiver->context);
            }
            consumed = true;
            break;
        case InputKeyRight:
            consumed = true;
            break;
        case InputKeyOk:
            with_view_model(
                receiver->view,
                ProtoPirateReceiverModel * model,
                {
                    size_t item_count =
                        ProtoPirateReceiverMenuItemArray_size(model->history_item_arr);
                    if(item_count > 0) {
                        if(receiver->callback) {
                            receiver->callback(
                                ProtoPirateCustomEventViewReceiverOK, receiver->context);
                        }
                    }
                },
                false);
            consumed = true;
            break;
        case InputKeyBack:
            if(receiver->callback) {
                with_view_model(
                    receiver->view,
                    ProtoPirateReceiverModel * model,
                    {
                        for(size_t i = 0;
                            i < ProtoPirateReceiverMenuItemArray_size(model->history_item_arr);
                            i++) {
                            ProtoPirateReceiverMenuItem* item =
                                ProtoPirateReceiverMenuItemArray_get(model->history_item_arr, i);
                            furi_string_free(item->item_str);
                        }
                        ProtoPirateReceiverMenuItemArray_reset(model->history_item_arr);
                        model->history_item = 0;
                        model->list_offset = 0;
                    },
                    false);
                receiver->callback(ProtoPirateCustomEventViewReceiverBack, receiver->context);
            }
            consumed = true;
            break;
        default:
            break;
        }
    }

    return consumed;
}

void protopirate_view_receiver_enter(void* context) {
    furi_assert(context);
    UNUSED(context);
}

void protopirate_view_receiver_exit(void* context) {
    furi_assert(context);
    UNUSED(context);
}

ProtoPirateReceiver* protopirate_view_receiver_alloc() {
    ProtoPirateReceiver* receiver = malloc(sizeof(ProtoPirateReceiver));

    receiver->view = view_alloc();
    view_allocate_model(receiver->view, ViewModelTypeLocking, sizeof(ProtoPirateReceiverModel));
    view_set_context(receiver->view, receiver);
    view_set_draw_callback(receiver->view, (ViewDrawCallback)protopirate_view_receiver_draw);
    view_set_input_callback(receiver->view, protopirate_view_receiver_input);
    view_set_enter_callback(receiver->view, protopirate_view_receiver_enter);
    view_set_exit_callback(receiver->view, protopirate_view_receiver_exit);

    with_view_model(
        receiver->view,
        ProtoPirateReceiverModel * model,
        {
            ProtoPirateReceiverMenuItemArray_init(model->history_item_arr);
            model->frequency_str = furi_string_alloc();
            model->preset_str = furi_string_alloc();
            model->history_stat_str = furi_string_alloc();
            model->list_offset = 0;
            model->history_item = 0;
            model->rssi = -127.0f;
            model->external_radio = false;
            model->lock = ProtoPirateLockOff;
            model->lock_count = 0;
        },
        true);

    return receiver;
}

void protopirate_view_receiver_free(ProtoPirateReceiver* receiver) {
    furi_assert(receiver);

    with_view_model(
        receiver->view,
        ProtoPirateReceiverModel * model,
        {
            for(size_t i = 0; i < ProtoPirateReceiverMenuItemArray_size(model->history_item_arr);
                i++) {
                ProtoPirateReceiverMenuItem* item =
                    ProtoPirateReceiverMenuItemArray_get(model->history_item_arr, i);
                furi_string_free(item->item_str);
            }
            ProtoPirateReceiverMenuItemArray_clear(model->history_item_arr);
            furi_string_free(model->frequency_str);
            furi_string_free(model->preset_str);
            furi_string_free(model->history_stat_str);
        },
        false);

    view_free(receiver->view);
    free(receiver);
}

View* protopirate_view_receiver_get_view(ProtoPirateReceiver* receiver) {
    furi_assert(receiver);
    return receiver->view;
}

uint16_t protopirate_view_receiver_get_idx_menu(ProtoPirateReceiver* receiver) {
    furi_assert(receiver);
    uint16_t idx = 0;
    with_view_model(
        receiver->view, ProtoPirateReceiverModel * model, { idx = model->history_item; }, false);
    return idx;
}

void protopirate_view_receiver_set_idx_menu(ProtoPirateReceiver* receiver, uint16_t idx) {
    furi_assert(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateReceiverModel * model,
        {
            model->history_item = idx;
            size_t item_count = ProtoPirateReceiverMenuItemArray_size(model->history_item_arr);
            if(model->history_item >= item_count) {
                model->history_item = item_count > 0 ? item_count - 1 : 0;
            }
        },
        true);
    protopirate_view_receiver_update_offset(receiver);
}
