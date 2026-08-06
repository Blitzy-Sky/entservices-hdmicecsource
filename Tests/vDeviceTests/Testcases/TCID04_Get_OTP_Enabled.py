"""
/**
 * @file TCID04_Get_OTP_Enabled.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Reads the one-touch-play enabled
 *        state after enabling it.
 *
 * @testcase TCID04_Get_OTP_Enabled
 * @details Enables OTP with setOTPEnabled and then checks getOTPEnabled reports it as enabled.
 *
 * @precondition
 *  - A device under test - physical hardware or a QEMU target - is running WPEFramework
 *    with the org.rdk.HdmiCecSource plugin activated and reachable over JSON-RPC.
 *
 * @dependencies
 *  - utils.py - shared endpoint resolution, curl dispatch and logging helpers
 *  - HdmiCECSource_Curl.py - the JSON-RPC command strings this module dispatches
 *  - SuitManager.py - registers and runs this module
 *
 * @expected_result
 *  - Each of these APIs answers with the values this scenario expects:
 *      org.rdk.HdmiCecSource.setOTPEnabled
 *      org.rdk.HdmiCecSource.getOTPEnabled
 *
 * @pass_criteria
 *  - Every response matches its expected value and run_test() returns True.
 *
 * @failure_criteria
 *  - A response mismatch, a JSON-RPC or JSON parsing failure, an unreachable endpoint
 *    or an unavailable device-level prerequisite; run_test() then returns False.
 */
"""


import time
import os


import json
from utils import (
    send_curl_command,
        log_info,
        log_success,
        log_error,
        log_warning,
    log_with_timing
)
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    # Deterministic precondition for validation.
    send_curl_command(HdmiCecSourceApis.set_otp_enabled_true)

    log_info("Executing the curl command get OTP enabled")

    curl_response = send_curl_command(
        HdmiCecSourceApis.get_otp_enabled
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        body = json.loads(curl_response)
        result = body.get("result", {})
        if result.get("success") is True and result.get("enabled") is True:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID04_Get_OTP_Enabled Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True

        log_error(f"TCID04_Get_OTP_Enabled Failed ❌")
        return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID04_Get_OTP_Enabled Failed ❌")
        return False
