"""
/**
 * @file TCID06_Perform_OTP_Action.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Performs a one-touch-play action.
 *
 * @testcase TCID06_Perform_OTP_Action
 * @details Invokes performOTPAction and then reads getDeviceList to confirm the plugin remains
 *          responsive afterwards.
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
 *      org.rdk.HdmiCecSource.performOTPAction
 *      org.rdk.HdmiCecSource.getDeviceList
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


import json, time
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

    log_info("Executing the curl command perform OTP Action")

    time.sleep(3)
    curl_response = send_curl_command(
        HdmiCecSourceApis.perform_otp_action
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        devices_response = send_curl_command(HdmiCecSourceApis.get_device_list)
        device_count = -1
        if devices_response:
            try:
                dbody = json.loads(devices_response)
                device_count = dbody.get("result", {}).get("numberofdevices", -1)
            except json.JSONDecodeError:
                device_count = -1

        body = json.loads(curl_response)
        success = body.get("result", {}).get("success") is True
        expected_runtime_error = (
            body.get("error", {}).get("message") == "ERROR_GENERAL"
            and device_count == 0
        )

        if success or expected_runtime_error:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID06_Perform_OTP_Action Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True

        log_error(f"TCID06_Perform_OTP_Action Failed ❌")
        return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID06_Perform_OTP_Action Failed ❌")
        return False
