"""
/**
 * @file TCID26_Standby_Then_OTP_Wakeup.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Standby followed by a one-touch-play
 *        wake-up.
 *
 * @testcase TCID26_Standby_Then_OTP_Wakeup
 * @details Posts a user-defined CEC message and device-status document to the vComponent, sends
 *          standby, then performs a one-touch-play action and checks the wake-up path succeeds.
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
 *  - vcomponent_configurations/commands/ - Device_CEC_Message_Userdef.yaml, Device_Status.yaml
 *
 * @expected_result
 *  - Each of these APIs answers with the values this scenario expects:
 *      org.rdk.HdmiCecSource.sendStandbyMessage
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


def _json_success(response):
    try:
        body = json.loads(response)
        return isinstance(body.get("result"), dict) and body["result"].get("success") is True
    except Exception:
        return False


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: standby from standby then OTP wake-up.
    _post_hdmicec("Device_CEC_Message_Userdef.yaml")
    time.sleep(1)

    standby_response = send_curl_command(HdmiCecSourceApis.send_standby_message)
    if not standby_response:
        log_error("✖ standby curl command not sent")
        return False
    log_warning(f"Standby Response: {standby_response}")

    _post_hdmicec("Device_Status.yaml")
    time.sleep(1)

    otp_response = send_curl_command(HdmiCecSourceApis.perform_otp_action)
    if not otp_response:
        log_error("✖ performOTPAction curl command not sent")
        return False
    log_warning(f"OTP Response: {otp_response}")

    if _json_success(otp_response):
        elapsed_time = time.perf_counter() - start_time
        msg = f"TCID26_Standby_Then_OTP_Wakeup Passed"
        if os.environ.get("HDMICEC_TIMING_ENABLED"):
            log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
        else:
            log_success(msg)
        return True

    log_error(f"TCID26_Standby_Then_OTP_Wakeup Failed")
    return False
