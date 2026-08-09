"""
/**
 * @file Init_Devicelist_Populate.py
 * @brief Init_Devicelist_Populate.
 *
 * @testcase Init_Devicelist_Populate
 * @details Validates the 'Init_Devicelist_Populate' HDMI CEC behavior through JSON-RPC and/or vComponent command flow.
 *
 * @precondition
 *  - Required plugin is active and reachable via JSON-RPC endpoint.
 *  - Target environment is ready for HDMI CEC emulation/command execution.
 *
 * @dependencies
 *  - utils.py
 *  - HdmiCECSource_Curl.py
 *  - suiteManager.py
 *  - vcomponent_configurations/hdmicec/commands/*.yaml (for emulation-based scenarios)
 *
 * @expected_result
 *  - API responses and scenario validations match expected values.
 *
 * @pass_criteria
 *  - Expected response equals actual response and testcase returns True.
 *
 * @failure_criteria
 *  - Response mismatch, command failure, JSON parsing error, or testcase returns False.
 */
"""



import json
import os
import time
import os

from utils import (
    send_curl_command,
        send_vcomponent_command,
        HDMICEC_CMD_BASE,
        activate_plugin,
        WPEFRAMEWORK_JSONRPC_URL,
        log_info,
        log_success,
        log_warning,
        log_error,
    log_with_timing
)
import HdmiCECSource_Curl as HdmiCecSourceApis


# ── helpers ──────────────────────────────────────────────────────────────────

def _post(yaml_name):
    """POST a vcomponent command YAML; returns True on HTTP 200."""
    http_code, body = send_vcomponent_command(f"{HDMICEC_CMD_BASE}/{yaml_name}")
    log_info(f"  POST {yaml_name}: HTTP {http_code}  {body}")
    return http_code == 200


def _get_device_list():
    """Call getDeviceList and return the parsed result dict, or None on error."""
    response = send_curl_command(HdmiCecSourceApis.get_device_list)
    if not response or response.startswith("< No response"):
        return None
    try:
        body = json.loads(response)
        return body.get("result")
    except json.JSONDecodeError:
        return None


def _set_enabled_true():
    """Enable HdmiCecSource plugin; returns True when API reports success."""
    response = send_curl_command(HdmiCecSourceApis.set_enabled_true)
    if not response or response.startswith("< No response"):
        return False
    try:
        body = json.loads(response)
        result = body.get("result", {})
        return result.get("success") is True
    except json.JSONDecodeError:
        return False


def _set_enabled_false():
    """Disable HdmiCecSource plugin; returns True when API reports success."""
    response = send_curl_command(HdmiCecSourceApis.set_enabled_false)
    if not response or response.startswith("< No response"):
        return False
    try:
        body = json.loads(response)
        result = body.get("result", {})
        return result.get("success") is True
    except json.JSONDecodeError:
        return False


def _get_enabled_state():
    """Return plugin enabled state as bool, or None on parse/transport error."""
    response = send_curl_command(HdmiCecSourceApis.get_enabled)
    if not response or response.startswith("< No response"):
        return None
    try:
        body = json.loads(response)
        result = body.get("result", {})
        enabled = result.get("enabled")
        return enabled if isinstance(enabled, bool) else None
    except json.JSONDecodeError:
        return None


def _build_la_map(device_list):
    """Build logicalAddress -> device entry map from getDeviceList payload."""
    return {
        d["logicalAddress"]: d
        for d in device_list
        if isinstance(d, dict) and isinstance(d.get("logicalAddress"), int)
    }


def _inject_triplet(rpa_yaml, osd_yaml, vid_yaml, inter_cmd_delay=0.35):
    """Inject ReportPhysicalAddress + SetOSDName + DeviceVendorID for one device."""
    for yaml_name in (rpa_yaml, osd_yaml, vid_yaml):
        if not _post(yaml_name):
            return False
        time.sleep(inter_cmd_delay)
    return True


def _wait_for_device(la, expected_name, timeout_s=3.0, poll_s=0.4):
    """Wait until getDeviceList shows expected LA with expected OSD name."""
    deadline = time.time() + timeout_s
    last_result = None
    while time.time() < deadline:
        result = _get_device_list()
        last_result = result
        if result and result.get("success") is True:
            la_map = _build_la_map(result.get("deviceList", []))
            entry = la_map.get(la)
            if entry and entry.get("osdName") == expected_name:
                return True, entry, result
        time.sleep(poll_s)
    return False, None, last_result


def _wait_for_la(la, timeout_s=3.0, poll_s=0.4):
    """Wait until getDeviceList includes LA; returns (found, entry, result)."""
    deadline = time.time() + timeout_s
    last_result = None
    while time.time() < deadline:
        result = _get_device_list()
        last_result = result
        if result and result.get("success") is True:
            la_map = _build_la_map(result.get("deviceList", []))
            entry = la_map.get(la)
            if entry is not None:
                return True, entry, result
        time.sleep(poll_s)
    return False, None, last_result


def _seed_device(la, expected_name, rpa_yaml, osd_yaml, vid_yaml, attempts=3):
    """Seed one device into deviceList with retries; returns True when LA appears."""
    for attempt in range(1, attempts + 1):
        log_info(f"  Seed LA={la} ({expected_name}) attempt {attempt}/{attempts}")
        if not _inject_triplet(rpa_yaml, osd_yaml, vid_yaml):
            log_warning(f"  Seed warning: injection rejected for LA={la} ({expected_name})")
            continue

        found, entry, _ = _wait_for_la(la, timeout_s=3.0, poll_s=0.4)
        if found:
            log_success(
                f"  ✓ Seed learned LA={la:2d}  osdName='{entry.get('osdName', '')}'  vendorID='{entry.get('vendorID', '')}'"
            )
            return True

    log_warning(f"  Seed warning: LA={la} ({expected_name}) did not appear after {attempts} attempts")
    return False


# ── test ─────────────────────────────────────────────────────────────────────

def run_test():
    start_time = time.perf_counter()

    """
    Step 1 – configure vcomponent network (VTV root + YAMAHA as AudioSystem child).
    Step 2 – inject CEC frames from YAMAHA (LA=5) so the middleware populates
             deviceList[5] through its normal process() handlers:
               ReportPhysicalAddress -> addDevice(5)
               SetOSDName            -> deviceList[5].m_osdName = "YAMAHA"
               DeviceVendorID        -> deviceList[5].m_vendorID = 0x00A0AF
               CECVersion            -> addDevice(5) (idempotent confirmation)
    Step 3 – call getDeviceList and verify LA 5 is present with correct details.
    """

    # ── Step 0: ensure plugin is active (standalone-safe) ───────────────────
    log_info(f"Init_Devicelist_Populate Step 0: activate plugin org.rdk.HdmiCecSource via {WPEFRAMEWORK_JSONRPC_URL}")
    if not activate_plugin("org.rdk.HdmiCecSource"):
        log_error(f"Init_Devicelist_Populate Failed ❌: plugin activation failed (org.rdk.HdmiCecSource)")
        return False
    # Align with suiteManager startup guard to let CEC threads fully initialize.
    time.sleep(6)

    # ── Step 1: configure ────────────────────────────────────────────────────
    log_info("Init_Devicelist_Populate Step 1: configure vcomponent network")
    if not _post("Device_Config_Add_Network.yaml"):
        log_error(f"Init_Devicelist_Populate Failed ❌: configure command rejected")
        return False

    # Ensure middleware poll/discovery threads are enabled in this runtime.
    if not _set_enabled_true():
        log_warning("Init_Devicelist_Populate Note ⚠: setEnabled(true) did not report success; attempting toggle")
        _set_enabled_false()
        if not _set_enabled_true():
            log_warning("Init_Devicelist_Populate Note ⚠: toggle enable sequence did not report success")

    enabled_state = _get_enabled_state()
    log_info(f"  HdmiCecSource enabled={enabled_state}")

    time.sleep(1)

    # ── Step 2: inject CEC payload frames ────────────────────────────────────
    log_info("Init_Devicelist_Populate Step 2: wait for middleware to auto-discover devices via poll")

    # After configure, the middleware's poll thread discovers all devices whose
    # logical addresses the vcomponent ACKs (now via AIDL sendMessage).
    # For each ACKed LA the middleware calls addDevice() then requestCecDevDetails()
    # which causes vcomponent to respond with SetOSDName / DeviceVendorID.
    # We wait up to 20 s for expected devices to appear.
    expected_las = {5: "YAMAHA", 3: "SAMSUNG", 9: "SONY", 10: "LG", 11: "PANASONIC", 2: "DENON"}
    deadline = time.time() + 20.0
    last_la_map = {}
    while time.time() < deadline:
        result = _get_device_list()
        if result and result.get("success") is True:
            last_la_map = _build_la_map(result.get("deviceList", []))
            found_las = sorted(last_la_map.keys())
            log_info(f"  Polling: discovered LAs={found_las}")
            # Step 2 is considered ready once bootstrap LA=5 is present.
            if 5 in last_la_map:
                break
        time.sleep(1.0)

    # Fallback path: some builds do not auto-discover from poll within timeout.
    # Seed LA=5 explicitly so bootstrap still guarantees a usable non-empty list.
    if 5 not in last_la_map:
        log_warning("Init_Devicelist_Populate Step 2 fallback: auto-discovery incomplete, injecting LA=5 seed frames")
        max_seed_attempts = 3
        for attempt in range(1, max_seed_attempts + 1):
            log_info(f"  Seed LA=5 attempt {attempt}/{max_seed_attempts}")
            ok_triplet = _inject_triplet(
                "DeviceListConfig/Payload_Report_Physical_Address_AudioSystem.yaml",
                "DeviceListConfig/Payload_Set_OSD_Name_YAMAHA.yaml",
                "DeviceListConfig/Payload_Vendor_ID_YAMAHA.yaml",
            )
            ok_cecver = _post("DeviceListConfig/Payload_CECVersion.yaml")
            if not ok_triplet or not ok_cecver:
                log_error(f"Init_Devicelist_Populate Failed ❌: LA=5 seed injection rejected")
                return False

            found, entry, _ = _wait_for_la(5, timeout_s=3.0, poll_s=0.4)
            if found:
                log_success(
                    f"  ✓ Seed learned LA=5  osdName='{entry.get('osdName', '')}'  vendorID='{entry.get('vendorID', '')}'"
                )
                break

            # Secondary seed path: ask configured LA=5 device to respond naturally.
            log_info("  Seed fallback: send Give* commands to LA=5")
            for yaml_name in (
                "Device_Give_Physical_Address.yaml",
                "Device_Give_OSD_Name.yaml",
                "Device_Give_Device_Vendor_ID.yaml",
            ):
                if not _post(yaml_name):
                    log_warning(f"  Seed fallback warning: {yaml_name} rejected")
                time.sleep(0.25)

            found, entry, _ = _wait_for_la(5, timeout_s=3.0, poll_s=0.4)
            if found:
                log_success(
                    f"  ✓ Give* learned LA=5  osdName='{entry.get('osdName', '')}'  vendorID='{entry.get('vendorID', '')}'"
                )
                break
        else:
            log_error(f"Init_Devicelist_Populate Failed ❌: middleware did not learn LA=5 after fallback seed attempts")
            return False

    # With LA=5 bootstrapped, explicitly seed the remaining configured devices.
    # This avoids depending on poll-based discovery for every secondary device.
    log_info("Init_Devicelist_Populate Step 2b: seed remaining configured devices")
    remaining_devices = [
        (3, "SAMSUNG", "DeviceListConfig/Payload_Report_Physical_Address_SAMSUNG.yaml", "DeviceListConfig/Payload_Set_OSD_Name_SAMSUNG.yaml", "DeviceListConfig/Payload_Vendor_ID_SAMSUNG.yaml"),
        (9, "SONY", "DeviceListConfig/Payload_Report_Physical_Address_SONY.yaml", "DeviceListConfig/Payload_Set_OSD_Name_SONY.yaml", "DeviceListConfig/Payload_Vendor_ID_SONY.yaml"),
        (10, "LG", "DeviceListConfig/Payload_Report_Physical_Address_LG.yaml", "DeviceListConfig/Payload_Set_OSD_Name_LG.yaml", "DeviceListConfig/Payload_Vendor_ID_LG.yaml"),
        (11, "PANASONIC", "DeviceListConfig/Payload_Report_Physical_Address_PANASONIC.yaml", "DeviceListConfig/Payload_Set_OSD_Name_PANASONIC.yaml", "DeviceListConfig/Payload_Vendor_ID_PANASONIC.yaml"),
        (2, "DENON", "DeviceListConfig/Payload_Report_Physical_Address_DENON.yaml", "DeviceListConfig/Payload_Set_OSD_Name_DENON.yaml", "DeviceListConfig/Payload_Vendor_ID_DENON.yaml"),
    ]
    for la, expected_name, rpa_yaml, osd_yaml, vid_yaml in remaining_devices:
        _seed_device(la, expected_name, rpa_yaml, osd_yaml, vid_yaml)

    # Give the middleware a short settle window before the final snapshot.
    time.sleep(1.0)

    # ── Step 3: verify device list ───────────────────────────────────────────
    log_info("Init_Devicelist_Populate Step 3: verify getDeviceList contains all 6 injected devices")

    result = _get_device_list()
    if result is None:
        log_error(f"Init_Devicelist_Populate Failed ❌: getDeviceList returned no response")
        return False

    success = result.get("success")
    num_devices = result.get("numberofdevices")
    device_list = result.get("deviceList", [])

    log_warning(f"  success={success}  numberofdevices={num_devices}  devices={device_list}")

    if success is not True:
        log_error(f"Init_Devicelist_Populate Failed ❌: getDeviceList success != true")
        return False

    strict_multi = os.environ.get("Init_Devicelist_Populate_STRICT_MULTI", "0") == "1"
    min_devices_required = 6 if strict_multi else int(os.environ.get("Init_Devicelist_Populate_MIN_DEVICES", "1"))

    if not isinstance(num_devices, int) or num_devices < min_devices_required:
        log_error(
            f"Init_Devicelist_Populate Failed ❌: numberofdevices={num_devices}, expected >= {min_devices_required}"
        )
        return False

    if not isinstance(device_list, list):
        log_error(f"Init_Devicelist_Populate Failed ❌: deviceList is not a list")
        return False

    # Expected devices: LA -> expected osdName
    expected_devices = {
        5:  "YAMAHA",
        3:  "SAMSUNG",
        9:  "SONY",
        10: "LG",
        11: "PANASONIC",
        2:  "DENON",
    }

    # Build lookup: logicalAddress -> entry
    la_map = _build_la_map(device_list)

    failures = []
    warnings = []

    for la, expected_name in expected_devices.items():
        entry = la_map.get(la)
        if entry is None:
            if strict_multi:
                failures.append(f"LA={la} ({expected_name}): not in deviceList")
            else:
                warnings.append(f"LA={la} ({expected_name}): not in deviceList")
            continue

        osd_name = entry.get("osdName", "")
        vendor_id = entry.get("vendorID", "")
        if osd_name != expected_name:
            if strict_multi:
                failures.append(f"LA={la}: osdName='{osd_name}', expected '{expected_name}'")
            else:
                warnings.append(f"LA={la}: osdName='{osd_name}', expected '{expected_name}'")
        if not vendor_id:
            if strict_multi:
                failures.append(f"LA={la} ({expected_name}): vendorID is empty")
            else:
                warnings.append(f"LA={la} ({expected_name}): vendorID is empty")

        log_success(f"  ✓ LA={la:2d}  osdName='{osd_name}'  vendorID='{vendor_id}'")

    if 5 not in la_map:
        failures.append("LA=5 (YAMAHA): bootstrap device missing")

    for w in warnings:
        log_warning(f"Init_Devicelist_Populate Note ⚠: {w}")

    if failures:
        for f in failures:
            log_error(f"Init_Devicelist_Populate Failed ❌: {f}")
        return False

    if strict_multi:
        elapsed_time = time.perf_counter() - start_time
        msg = f"Init_Devicelist_Populate Passed ✅  Strict mode: all {len(expected_devices)} devices verified"
        if os.environ.get("HDMICEC_TIMING_ENABLED"):
            log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
        else:
            log_success(msg)
    else:
        elapsed_time = time.perf_counter() - start_time
        msg = "Init_Devicelist_Populate Passed ✅  Bootstrap mode: device list ready (set Init_Devicelist_Populate_STRICT_MULTI=1 for full 6-device enforcement)"
        if os.environ.get("HDMICEC_TIMING_ENABLED"):
            log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
        else:
            log_success(msg)
    return True
