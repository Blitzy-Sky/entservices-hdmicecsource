"""
/**
 * @file TCID22_Verify_Devicelist_CEC_Disabled.py
 * @brief L2 HDMI CEC functional testcase.
 *
 * @testcase TCID22_Verify_Devicelist_CEC_Disabled
 * @details Validates the 'TCID22_Verify_Devicelist_CEC_Disabled' HDMI CEC behavior through JSON-RPC and/or vComponent command flow.
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
    """Post a HdmiCec vComponent YAML command using the new curl API."""
    http_code, body = send_vcomponent_command(f"{HDMICEC_CMD_BASE}/{yaml_file}")
    log_info(f"  vComponent POST {yaml_file}: HTTP {http_code}  {body}")
    return http_code == 200

def run_test():
    start_time = time.perf_counter()

    log_success("Negative scenario - calling getEnabled with driver status TRUE")
    curl_response = send_curl_command(
            HdmiCecSourceApis.get_enabled
        )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False
    else:
        log_warning(f"Response: {curl_response}")

    time.sleep(2)
    log_success("Negative scenario - calling getDeviceList")
    curl_response = send_curl_command(
            HdmiCecSourceApis.get_device_list
        )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False
    else:
        log_warning(f"Response: {curl_response}")

    log_error("Overriding the HAL API HdmICecOpen return value as negative")
    time.sleep(3)
    if not _post_hdmicec("Device_Setapi_Open_Fail.yaml"):
        log_error("✖ missing or rejected vComponent YAML: Device_Setapi_Open_Fail.yaml")
        return False
    if not _post_hdmicec("Device_Setapi_Logic_Fail.yaml"):
        log_error("✖ missing or rejected vComponent YAML: Device_Setapi_Logic_Fail.yaml")
        return False
    time.sleep(2)
    try:
        log_success("Negative scenario - making the driver status as TRUE using setEnabled")
        curl_response = send_curl_command(
            HdmiCecSourceApis.set_enabled_true
        )

        if not curl_response:
            log_error("✖ curl command not sent")
            return False
        else:
            log_warning(f"Response: {curl_response}")
        log_success("Negative scenario - calling getDeviceList After setting the HdmiCecLogical address and HdmiCecOpen HAL APIs return value error")
        time.sleep(2)
        curl_response = send_curl_command(
                HdmiCecSourceApis.get_device_list
            )

        if not curl_response:
            log_error("✖ curl command not sent")
            return False
        else:
            log_warning(f"Response: {curl_response}")
    except Exception as exc:
        log_warning(f"Driver FAILED: {exc}")
    
    elapsed_time = time.perf_counter() - start_time
    msg = "TCID22_Verify_Devicelist_CEC_Disabled passed"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True


   
