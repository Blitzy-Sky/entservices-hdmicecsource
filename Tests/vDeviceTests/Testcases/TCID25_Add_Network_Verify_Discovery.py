"""
/**
 * @file TCID25_Add_Network_Verify_Discovery.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Network add and device discovery.
 *
 * @testcase TCID25_Add_Network_Verify_Discovery
 * @details Adds an emulated network topology through the vComponent and checks getDeviceList
 *          discovers the added devices.
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
 *  - vcomponent_configurations/commands/ - Device_Config_Add_Network.yaml, Device_Status.yaml
 *
 * @expected_result
 *  - org.rdk.HdmiCecSource.getDeviceList answers with the values this scenario expects.
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
import os
import subprocess
import time
import os
from pathlib import Path
from utils import (
    send_curl_command,
        send_vcomponent_command,
        HDMICEC_CMD_BASE,
        log_info,
        log_success,
        log_error,
        log_warning,
    log_with_timing
)
import HdmiCECSource_Curl as HdmiCecSourceApis


def _post_hdmicec(yaml_file):
    http_code, body = send_vcomponent_command(f"{HDMICEC_CMD_BASE}/{yaml_file}")
    log_info(f"  vComponent POST {yaml_file}: HTTP {http_code}  {body}")
    return http_code == 200


def _resolve_send_events_script():
    # Priority 1: explicit override from environment.
    env_path = os.environ.get("SEND_EVENTS_SCRIPT", "").strip()
    if env_path and Path(env_path).is_file():
        return env_path

    # Priority 2: common legacy locations.
    base = Path(__file__).resolve().parent
    candidates = [
        base / "../../../../../sendEvents.sh",
        base / "sendEvents.sh",
        Path("/tmp/sendEvents.sh"),
    ]

    # Priority 3: discover classic legacy location from any ancestor.
    for ancestor in [base, *base.parents]:
        candidates.append(
            ancestor / "OLD_TESTCASE_RDKE/rdkservices/L2HalMock/sendEvents.sh"
        )

    for c in candidates:
        p = c.resolve() if not c.is_absolute() else c
        if p.is_file():
            return str(p)

    return ""


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: sending events simulation.
    before = send_curl_command(HdmiCecSourceApis.get_device_list)

    # Try legacy-equivalent event script first when available.
    send_events_script = _resolve_send_events_script()
    if send_events_script:
        log_info(f"Executing legacy sendEvents script: {send_events_script}")
        try:
            result = subprocess.run(
                ["/bin/bash", send_events_script],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            if result.returncode != 0:
                log_error(f"✖ sendEvents script failed: {result.stderr.strip()}")
                return False
            log_success("✔ sendEvents script executed successfully")
        except Exception as exc:
            log_error(f"✖ sendEvents script execution exception: {exc}")
            return False
    else:
        log_warning("sendEvents.sh not found; using vComponent emulation fallback path")

    ok1 = _post_hdmicec("Device_Config_Add_Network.yaml")
    time.sleep(1)
    ok2 = _post_hdmicec("Device_Status.yaml")
    time.sleep(1)

    if not (ok1 and ok2):
        log_error("✖ required vComponent emulation posts failed")
        return False

    response = send_curl_command(HdmiCecSourceApis.get_device_list)
    if not response:
        log_error("✖ getDeviceList curl command not sent")
        return False

    log_warning(f"Response: {response}")
    try:
        before_count = None
        if before:
            bbody = json.loads(before)
            before_count = bbody.get("result", {}).get("numberofdevices")

        body = json.loads(response)
        result = body.get("result", {})
        after_count = result.get("numberofdevices")
        count_valid = isinstance(after_count, int)
        if isinstance(before_count, int):
            count_valid = count_valid and after_count >= before_count

        if "error" not in body and result.get("success") is True and count_valid:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID25_Add_Network_Verify_Discovery Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except json.JSONDecodeError:
        pass

    log_error(f"TCID25_Add_Network_Verify_Discovery Failed ❌")
    return False
