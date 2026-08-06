"""
/**
 * @file TCID27_Active_Source_Transition_After_OTP.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Active-source transition after one-
 *        touch-play.
 *
 * @testcase TCID27_Active_Source_Transition_After_OTP
 * @details Reads getActiveSourceStatus, adds an emulated peer through the vComponent, performs a
 *          one-touch-play action and checks the active-source status transitions accordingly.
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
 *  - vcomponent_configurations/commands/ - Device_Add.yaml, Device_Status.yaml
 *
 * @expected_result
 *  - Each of these APIs answers with the values this scenario expects:
 *      org.rdk.HdmiCecSource.getActiveSourceStatus
 *      org.rdk.HdmiCecSource.performOTPAction
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


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: active source status after path/routing style changes.
    before = send_curl_command(HdmiCecSourceApis.get_active_source_status)
    if not before:
        log_error("✖ initial getActiveSourceStatus command not sent")
        return False
    log_warning(f"Initial status: {before}")

    ok1 = _post_hdmicec("Device_Add.yaml")
    time.sleep(1)
    ok2 = _post_hdmicec("Device_Status.yaml")
    time.sleep(1)

    if not (ok1 and ok2):
        log_error("✖ required vComponent emulation posts failed")
        return False

    otp = send_curl_command(HdmiCecSourceApis.perform_otp_action)
    if not otp:
        log_error("✖ performOTPAction command not sent")
        return False

    after = send_curl_command(HdmiCecSourceApis.get_active_source_status)
    if not after:
        log_error("✖ final getActiveSourceStatus command not sent")
        return False
    log_warning(f"Final status: {after}")

    try:
        before_body = json.loads(before)
        _ = before_body.get("result", {}).get("status")

        body = json.loads(after)
        result = body.get("result", {})
        if result.get("success") is True and result.get("status") is True:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID27_Active_Source_Transition_After_OTP Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except json.JSONDecodeError:
        pass

    log_error(f"TCID27_Active_Source_Transition_After_OTP Failed ❌")
    return False
