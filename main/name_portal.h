/**
 * @file name_portal.h
 * @brief One-shot SoftAP + HTTP form for entering the badge nickname
 *        from a phone. Modeled on the BYUI loader's wifi_config + portal_mode
 *        pattern but stripped to "just the name" — no SSID/password,
 *        no captive portal, just one open AP and one form.
 *
 * Flow:
 *   1. name_portal_run() brings up an open SoftAP at 192.168.4.1
 *   2. Renders a QR code for http://192.168.4.1/ on the display
 *   3. Phone scans, joins, browses, submits a single text field
 *   4. Submitted name is stored in NVS (badge_cfg / nick) and
 *      placed in @p nick_out
 *   5. Function returns; caller continues
 *
 * @param nick_out  Destination buffer for the submitted nickname.
 * @param outlen    Capacity of nick_out (≥ 33 recommended).
 * @return          true if a name was submitted, false on cancel/timeout.
 */

#pragma once
#include <stdbool.h>
#include <stddef.h>

bool name_portal_run(char *nick_out, size_t outlen);
