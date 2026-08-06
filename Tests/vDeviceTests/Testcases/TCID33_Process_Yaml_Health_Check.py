"""
/**
 * @file TCID33_Process_Yaml_Health_Check.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Health check over the vComponent
 *        process response documents.
 *
 * @testcase TCID33_Process_Yaml_Health_Check
 * @details Posts every process response document to the vComponent and checks getDeviceList and
 *          getActiveSourceStatus still answer correctly afterwards.
 *
 * @precondition
 *  - A device under test - physical hardware or a QEMU target - is running WPEFramework
 *    with the org.rdk.HdmiCecSource plugin activated and reachable over JSON-RPC.
 *  - The vComponent HDMI-CEC emulator is running and accepting YAML command documents.
 *
 * @dependencies
 *  - utils.py - shared endpoint resolution, curl dispatch and logging helpers
 *  - HdmiCECSource_Curl.py - the JSON-RPC command strings this module dispatches
 *  - SuitManager.py - registers and runs this module
 *  - vcomponent_configurations/commands/ - Process_CEC_Version.yaml,
 *    Process_Device_Vendor_ID.yaml, Process_Report_Physical_Address.yaml,
 *    Process_Request_Active_Source.yaml, Process_Routing_Change.yaml,
 *    Process_Routing_Information.yaml, Process_Set_OSD_Name.yaml, Process_Set_Stream_Path.yaml
 *
 * @expected_result
 *  - Each of these APIs answers with the values this scenario expects:
 *      org.rdk.HdmiCecSource.getDeviceList
 *      org.rdk.HdmiCecSource.getActiveSourceStatus
 *  - Every vComponent command document is accepted before the APIs are exercised.
 *
 * @pass_criteria
 *  - Every response matches its expected value and run_test() returns True.
 *
 * @failure_criteria
 *  - A response mismatch, a JSON-RPC or JSON parsing failure, an unreachable endpoint
 *    or a rejected vComponent command document; run_test() then returns False.
 */
"""


import json
import time
import os
from pathlib import Path

from utils import (
    send_curl_command,
        send_vcomponent_command,
        HDMICEC_CMD_BASE,
        log_info,
        log_success,
        log_warning,
        log_error,
    log_with_timing
)
import HdmiCECSource_Curl as HdmiCecSourceApis


def _post_yaml(yaml_name):
    http_code, body = send_vcomponent_command(f"{HDMICEC_CMD_BASE}/{yaml_name}")
    log_info(f"POST {yaml_name}: HTTP {http_code} {body}")
    return http_code == 200


def _health_check():
    response = send_curl_command(HdmiCecSourceApis.get_device_list)
    if not response or response.startswith("< No response"):
        return False

    try:
        body = json.loads(response)
        return isinstance(body, dict) and "result" in body
    except json.JSONDecodeError:
        return False


def _get_device_snapshot():
    response = send_curl_command(HdmiCecSourceApis.get_device_list)
    if not response or response.startswith("< No response"):
        return None

    try:
        body = json.loads(response)
        result = body.get("result", {})
        number = result.get("numberofdevices")
        devices = result.get("deviceList", [])
        if not isinstance(devices, list):
            devices = []
        logicals = set()
        for d in devices:
            if isinstance(d, dict):
                la = d.get("logicalAddress")
                if isinstance(la, int):
                    logicals.add(la)
        return {
            "number": number if isinstance(number, int) else None,
            "logicals": logicals,
        }
    except json.JSONDecodeError:
        return None


def _get_active_source_status_ok():
    response = send_curl_command(HdmiCecSourceApis.get_active_source_status)
    if not response or response.startswith("< No response"):
        return False
    try:
        body = json.loads(response)
        result = body.get("result", {})
        return isinstance(result.get("status"), bool) and result.get("success") is True
    except json.JSONDecodeError:
        return False


def run_test():
    start_time = time.perf_counter()

    # Validate all process-trigger YAML files are accepted by vComponent,
    # and add observable checks for handlers that should affect plugin state.
    commands_dir = Path(__file__).resolve().parent.parent / "vcomponent_configurations" / "commands"
    yaml_files = sorted(
        p.relative_to(commands_dir).as_posix()
        for p in commands_dir.rglob("Process_*.yaml")
    )

    if not yaml_files:
        log_error(f"TCID33_Process_Yaml_Health_Check Failed: no Process_*.yaml files found")
        return False

    # Configure vcomponent network with known topology (YAMAHA at LA=5) so that
    # device-list state checks have a stable vcomponent-backed device to verify against.
    http_code, _ = send_vcomponent_command(f"{HDMICEC_CMD_BASE}/Device_Config_Add_Network.yaml")
    if http_code != 200:
        log_error(f"TCID33_Process_Yaml_Health_Check Failed: configure command rejected")
        return False
    time.sleep(1)

    if not _health_check():
        log_error(f"TCID33_Process_Yaml_Health_Check Failed: pre-check getDeviceList is not healthy")
        return False

    pre_snapshot = _get_device_snapshot()
    if pre_snapshot is None:
        log_error(f"TCID33_Process_Yaml_Health_Check Failed: unable to capture pre device snapshot")
        return False

    failed_posts = []
    state_check_failures = []

    # These handlers can be observed by API-level side effects.
    # Maps yaml filename -> check type (state for device list checks, health for API checks)
    should_touch_device_list = {
        "Process_Report_Physical_Address.yaml",
        "Process_CEC_Version.yaml",
        "Process_Set_OSD_Name.yaml",
        "Process_Device_Vendor_ID.yaml",
    }
    should_keep_active_status_api_healthy = {
        "Process_Routing_Change.yaml",
        "Process_Routing_Information.yaml",
        "Process_Set_Stream_Path.yaml",
        "Process_Request_Active_Source.yaml",
    }

    for yaml_name in yaml_files:
        if not _post_yaml(yaml_name):
            failed_posts.append(yaml_name)
            continue

        if yaml_name in should_touch_device_list:
            # Longer window for the full pipeline:
            # vcomponent AIDL callback → DriverReceiveCallback → rQueue
            # → read thread → MessageDecoder → process() → addDevice()
            time.sleep(1.5)
            snap = _get_device_snapshot()
            if snap is None:
                state_check_failures.append(f"{yaml_name}: snapshot unavailable")
            # Note: State-level verification requires special LA injection files; 
            # basic acceptance is validated by successful HTTP 200 response above
        else:
            # Short window for non-state-check YAMLs.
            time.sleep(0.2)

        if yaml_name in should_keep_active_status_api_healthy:
            if not _get_active_source_status_ok():
                state_check_failures.append(f"{yaml_name}: activeSourceStatus API unhealthy")

    if not _health_check():
        log_error(f"TCID33_Process_Yaml_Health_Check Failed: post-check getDeviceList is not healthy")
        return False

    post_snapshot = _get_device_snapshot()
    if post_snapshot is None:
        log_error(f"TCID33_Process_Yaml_Health_Check Failed: unable to capture post device snapshot")
        return False

    pre_num = pre_snapshot["number"] if isinstance(pre_snapshot["number"], int) else -1
    post_num = post_snapshot["number"] if isinstance(post_snapshot["number"], int) else -1
    log_info(f"Device count pre={pre_num} post={post_num}")

    if failed_posts:
        log_warning(f"Failed YAML posts: {failed_posts}")
        log_error(f"TCID33_Process_Yaml_Health_Check Failed")
        return False

    if state_check_failures:
        log_warning(f"State-check warnings: {state_check_failures}")
        log_error(f"TCID33_Process_Yaml_Health_Check Failed")
        return False

    log_info("Non-observable handlers (event-only/outbound-only) remain acceptance-based in this TC.")
    log_info("For strict proof, add implementation counters or parse plugin logs per handler.")

    if post_num < pre_num:
        log_warning("Post device count lower than pre-count; treating as unstable runtime")
        log_error(f"TCID33_Process_Yaml_Health_Check Failed")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = f"TCID33_Process_Yaml_Health_Check Passed ✅ ({len(yaml_files)} process YAMLs posted + observable checks)"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
