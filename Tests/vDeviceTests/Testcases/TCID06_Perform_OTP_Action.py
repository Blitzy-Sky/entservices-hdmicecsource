"""
/**
 * @file TCID06_Perform_OTP_Action.py
 * @brief L2 HDMI CEC functional testcase.
 *
 * @testcase TCID06_Perform_OTP_Action
 * @details Validates the 'TCID06_Perform_OTP_Action' HDMI CEC behavior through JSON-RPC and/or vComponent command flow.
 *
 * @precondition
 *  - Required plugin is active and reachable via JSON-RPC endpoint.
 *  - Target environment is ready for HDMI CEC emulation/command execution.
 *
 * @dependencies
 *  - utils.py
 *  - HdmiCECSource_Curl.py
 *  - SuitManager.py
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
