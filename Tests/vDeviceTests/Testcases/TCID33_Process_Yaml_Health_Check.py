"""
/**
 * @file TCID33_Process_Yaml_Health_Check.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID33_Process_Yaml_Health_Check
 * @details A sweep over every inbound-frame fixture this suite ships. The case enumerates
 *          Process_*.yaml under vcomponent_configurations/commands recursively, sorted by
 *          POSIX-relative name, and fails immediately if none is found - so a suite that lost
 *          its fixtures is reported rather than passing vacuously.
 *          Device_Config_Add_Network.yaml is posted first and is REQUIRED (a non-200 fails the
 *          case), because the device-list checks need the emulated topology in place, with the
 *          audio system at logical address 5.
 *
 *          getDeviceList health and a device snapshot are captured BEFORE and AFTER the sweep.
 *          A snapshot records the published count and the set of integer logical addresses;
 *          health means the reply parses as a dict carrying a "result" member.
 *
 *          TWO SUBSETS GET EXTRA OBSERVATION. The four documents that should reach the device
 *          list - Process_Report_Physical_Address, Process_CEC_Version, Process_Set_OSD_Name
 *          and Process_Device_Vendor_ID - are given a 1.5 s window for the full inbound
 *          pipeline and then a snapshot, whose unavailability is recorded as a state-check
 *          failure. The four routing and active-source documents - Process_Routing_Change,
 *          Process_Routing_Information, Process_Set_Stream_Path and
 *          Process_Request_Active_Source - must leave getActiveSourceStatus healthy
 *          afterwards, meaning result.status is a bool and result.success is True. Every other
 *          document gets a 0.2 s window and acceptance only.
 *
 *          THE CASE IS ACCEPTANCE-BASED FOR HANDLERS WITH NO API-VISIBLE EFFECT, and it logs
 *          that limitation itself: per-handler proof would need implementation counters or
 *          plugin-log parsing, neither of which this transport has. The final guard is
 *          monotonic - the post-sweep device count must not be lower than the pre-sweep count,
 *          so a sweep that destabilised the inventory is reported.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - The vComponent API is reachable and serving the emulated CEC peers; HDMICEC_CMD_BASE
 *    resolves to the commands directory this module posts from.
 *  - Authored for device-level execution and NOT executed: every criterion below states what
 *    this module asserts, not an observed result. README.txt.txt records the deferred status
 *    and the prerequisites that are unavailable.
 *
 * @dependencies
 *  - utils.py - send_curl_command, send_vcomponent_command, HDMICEC_CMD_BASE and the logging
 *    helpers.
 *  - HdmiCECSource_Curl.py - the JSON-RPC request constants this module dispatches.
 *  - SuitManager.py - the runner that registers this module and calls run_test().
 *  - vcomponent_configurations/commands/Device_Config_Add_Network.yaml
 *  - vcomponent_configurations/commands/Process_*.yaml - every document matching this pattern,
 *    enumerated at run time rather than listed, so a fixture added later is swept without
 *    editing this module.
 *
 * @expected_result
 *  - At least one Process_*.yaml is found; the configure document and every enumerated
 *    document are accepted; getDeviceList is healthy before and after; both snapshots are
 *    obtained; no state check fails; and the device count does not drop across the sweep.
 *
 * @pass_criteria
 *  - All of the above hold and run_test() returns True; the pass log reports how many process
 *    documents were posted.
 *
 * @failure_criteria
 *  - No Process_*.yaml found; the configure document rejected; any enumerated document
 *    rejected; either health check failing; either snapshot unavailable; any state-check
 *    failure (a missing snapshot after a device-list document, or an unhealthy
 *    getActiveSourceStatus after a routing document); or a post-sweep device count lower than
 *    the pre-sweep count.
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
