"""
/**
 * @file TCID30_Repeated_Enable_Idempotent.py
 * @brief L2 HDMI CEC functional testcase.
 *
 * @testcase TCID30_Repeated_Enable_Idempotent
 * @details Validates the 'TCID30_Repeated_Enable_Idempotent' HDMI CEC behavior through JSON-RPC and/or vComponent command flow.
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


import json
from utils import send_curl_command, log_success, log_error, log_warning
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: getEnabled when already enabled.
    send_curl_command(HdmiCecSourceApis.set_enabled_true)
    first_get = send_curl_command(HdmiCecSourceApis.get_enabled)
    send_curl_command(HdmiCecSourceApis.set_enabled_true)
    second_get = send_curl_command(HdmiCecSourceApis.get_enabled)

    if not second_get:
        log_error("✖ getEnabled command not sent")
        return False

    log_warning(f"Final enabled response: {second_get}")
    try:
        body = json.loads(second_get)
        enabled = body.get("result", {}).get("enabled")
        if enabled is True:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID30_Repeated_Enable_Idempotent Passed"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except Exception:
        pass

    log_error(f"TCID30_Repeated_Enable_Idempotent Failed")
    return False
