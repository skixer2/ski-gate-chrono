/**
 * @file    bhy2_cal_hook.cpp
 * @brief   SGC calibration accuracy capture from BHY2 meta-events.
 *
 * PROBLEM: The BHI260AP reports calibration accuracy (0-3) via
 * BHY2_META_EVENT_SENSOR_STATUS meta-events. The Arduino_BHY2 library
 * receives them in BoschParser::parseMetaEvent() but only prints to
 * debug — the value is discarded.
 *
 * APPROACH: Register a second FIFO parse callback for
 * BHY2_SYS_ID_META_EVENT that stores accuracy before the library's
 * handler runs. To get bhy2_dev* (needed by bhy2_register_fifo_parse_callback),
 * we use `#define private public` to access BoschSensortec::_bhy2,
 * then immediately restore the macro.
 *
 * CALL: bhy2_cal_hook_init() once after BHY2.begin() in setup().
 *
 * SGC reads: extern volatile uint8_t g_bhy2_accuracy[256];
 *   Index 34 = SENSOR_ID_RV (Rotation Vector).
 */

#include <Arduino.h>
#define private public
#include "BoschSensortec.h"
#undef private

extern "C" {
#include "bosch/bhy2.h"
}

extern BoschSensortec sensortec;
volatile uint8_t g_bhy2_accuracy[256] = {0};
volatile uint8_t g_meta_event_count = 0;

static void sgc_meta_callback(const struct bhy2_fifo_parse_data_info *info, void *ref)
{
    (void)ref;
    g_meta_event_count++;
    if (info->data_size < 3) return;
    uint8_t type = info->data_ptr[0];
    if (type == BHY2_META_EVENT_SENSOR_STATUS) {
        uint8_t sensor_id = info->data_ptr[1];
        uint8_t accuracy  = info->data_ptr[2];
        g_bhy2_accuracy[sensor_id] = accuracy;
        Serial.print("[CAL] sensor ");
        Serial.print(sensor_id);
        Serial.print(" accuracy → ");
        Serial.println(accuracy);
    }
}

void bhy2_cal_hook_init()
{
    Serial.print("[CAL] Hooking BHY2 meta-events... ");

    bhy2_register_fifo_parse_callback(
        BHY2_SYS_ID_META_EVENT,
        sgc_meta_callback,
        nullptr,
        &sensortec._bhy2);

    bhy2_register_fifo_parse_callback(
        BHY2_SYS_ID_META_EVENT_WU,
        sgc_meta_callback,
        nullptr,
        &sensortec._bhy2);

    Serial.println("done");
}
