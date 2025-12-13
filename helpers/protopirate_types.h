// helpers/protopirate_types.h
#pragma once

#include <furi.h>
#include <furi_hal.h>

typedef enum
{
    ProtoPirateViewVariableItemList,
    ProtoPirateViewSubmenu,
    ProtoPirateViewWidget,
    ProtoPirateViewReceiver,
    ProtoPirateViewReceiverInfo,
    ProtoPirateViewAbout,
} ProtoPirateView;

typedef enum
{
    // Custom events for views
    ProtoPirateCustomEventViewReceiverOK,
    ProtoPirateCustomEventViewReceiverConfig,
    ProtoPirateCustomEventViewReceiverBack,
    ProtoPirateCustomEventViewReceiverUnlock,
    // Custom events for scenes
    ProtoPirateCustomEventSceneReceiverUpdate,
    ProtoPirateCustomEventSceneSettingLock,
    // File management
    ProtoPirateCustomEventReceiverInfoSave,
    ProtoPirateCustomEventSavedInfoDelete,
    // Emulator
    ProtoPirateCustomEventSavedInfoEmulate,
    ProtoPirateCustomEventEmulateTransmit,
    ProtoPirateCustomEventEmulateStop,
    ProtoPirateCustomEventEmulateExit,
} ProtoPirateCustomEvent;

typedef enum
{
    ProtoPirateLockOff,
    ProtoPirateLockOn,
} ProtoPirateLock;

typedef enum
{
    ProtoPirateTxRxStateIDLE,
    ProtoPirateTxRxStateRx,
    ProtoPirateTxRxStateTx,
    ProtoPirateTxRxStateSleep,
} ProtoPirateTxRxState;

typedef enum
{
    ProtoPirateHopperStateOFF,
    ProtoPirateHopperStateRunning,
    ProtoPirateHopperStatePause,
    ProtoPirateHopperStateRSSITimeOut,
} ProtoPirateHopperState;

typedef enum
{
    ProtoPirateRxKeyStateIDLE,
    ProtoPirateRxKeyStateBack,
    ProtoPirateRxKeyStateStart,
    ProtoPirateRxKeyStateAddKey,
} ProtoPirateRxKeyState;