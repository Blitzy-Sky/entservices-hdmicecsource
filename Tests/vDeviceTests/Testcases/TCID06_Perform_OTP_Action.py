"""
/**
 * @file TCID06_Perform_OTP_Action.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID06_Perform_OTP_Action
 * @details Waits three seconds for the plugin to settle, calls
 *          org.rdk.HdmiCecSource.performOTPAction - the only request in this suite given an
 *          eight second curl budget, because the plugin walks the bus behind it - and then
 *          reads getDeviceList to learn the device count.
 *
 *          TWO outcomes are admitted, and the second is the point of the case: result.success
 *          exactly True, OR an error whose message is exactly "ERROR_GENERAL" together with a
 *          device count of exactly 0. The plugin's PerformOTPAction requires CEC and the
 *          one-time-play setting to be enabled and a device to act on, so ERROR_GENERAL with
 *          an empty inventory is a correct target response rather than a defect. ERROR_GENERAL
 *          while devices ARE listed fails, and so does any other error.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - CEC and the one-time-play setting are enabled - both default to true and positions 04 and
 *    12 also write them.
 *  - Authored for device-level execution and NOT executed: every criterion below states what
 *    this module asserts, not an observed result. README.txt.txt records the deferred status
 *    and the prerequisites that are unavailable.
 *
 * @dependencies
 *  - utils.py - send_curl_command and the logging helpers.
 *  - HdmiCECSource_Curl.py - the JSON-RPC request constants this module dispatches.
 *  - SuitManager.py - the runner that registers this module and calls run_test().
 *
 * @expected_result
 *  - Either the walk is accepted (result.success true) or it is refused with ERROR_GENERAL
 *    while the published device count is 0.
 *
 * @pass_criteria
 *  - The OTP response is non-empty and either predicate holds, and run_test() returns True.
 *
 * @failure_criteria
 *  - The OTP request returns nothing; an error other than ERROR_GENERAL; ERROR_GENERAL while
 *    the device count is not 0; or a JSONDecodeError on the OTP reply.
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
