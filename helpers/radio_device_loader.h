// helpers/radio_device_loader.h
#pragma once

#include <lib/subghz/devices/devices.h>

/** SubGhzRadioDeviceType */
typedef enum {
    SubGhzRadioDeviceTypeInternal,
    SubGhzRadioDeviceTypeExternalCC1101,
} SubGhzRadioDeviceType;

bool radio_device_loader_is_connect_external(const char* name);

const SubGhzDevice* radio_device_loader_set(
    const SubGhzDevice* current_radio_device,
    SubGhzRadioDeviceType radio_device_type);

bool radio_device_loader_is_external(const SubGhzDevice* radio_device);

void radio_device_loader_end(const SubGhzDevice* radio_device);