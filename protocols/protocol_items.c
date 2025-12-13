// protocols/protocol_items.c
#include "protocol_items.h"

const SubGhzProtocol* protopirate_protocol_registry_items[] = {
    &kia_protocol_v0,
    &kia_protocol_v1,
    &kia_protocol_v2,
    &kia_protocol_v3_v4,
    &kia_protocol_v5,
    &ford_protocol_v0,
    &subaru_protocol,
    &suzuki_protocol,
    &vw_protocol,
};

const SubGhzProtocolRegistry protopirate_protocol_registry = {
    .items = protopirate_protocol_registry_items,
    .size = COUNT_OF(protopirate_protocol_registry_items),
};