"""
/**
 * @file TCID26_Standby_Then_OTP_Wakeup.py
 * @brief L2 HDMI CEC functional testcase.
 *
 * @testcase TCID26_Standby_Then_OTP_Wakeup
 * @details Validates the 'TCID26_Standby_Then_OTP_Wakeup' HDMI CEC behavior through JSON-RPC and/or vComponent command flow.
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
