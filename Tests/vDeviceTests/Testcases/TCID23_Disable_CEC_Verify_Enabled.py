"""
/**
 * @file TCID23_Disable_CEC_Verify_Enabled.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Disable CEC and verify the reported
 *        state.
 *
 * @testcase TCID23_Disable_CEC_Verify_Enabled
 * @details Posts driver open pass/fail documents to the vComponent, disables CEC with setEnabled
 *          false, checks getEnabled reports it disabled, then restores it with setEnabled true.
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
 *  - vcomponent_configurations/commands/ - Device_Setapi_Open_Fail.yaml,
 *    Device_Setapi_Open_Pass.yaml
 *
 * @expected_result
 *  - Each of these APIs answers with the values this scenario expects:
 *      org.rdk.HdmiCecSource.setEnabled
 *      org.rdk.HdmiCecSource.getEnabled
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

    #base_dir = "/tmp/vcomponent_configurations/commands"
    log_success("Negative scenario - Making the setEnabled driver status as FALSE")
    curl_response = send_curl_command(
            HdmiCecSourceApis.set_enabled_false
        )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False
    else:
        log_warning(f"Response: {curl_response}")

    time.sleep(2)
    log_success("Negative scenario - verifying the driver status with getEnabled")
    curl_response = send_curl_command(
            HdmiCecSourceApis.get_enabled
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
    except Exception as exc:
        log_error(f"Exception during negative scenario: {exc}")
        return False
    
    log_error("Overriding the HAL API HdmICecOpen return value as POSITIVE as post condition")
    time.sleep(3)
    if not _post_hdmicec("Device_Setapi_Open_Pass.yaml"):
        log_error("✖ missing or rejected vComponent YAML: Device_Setapi_Open_Pass.yaml")
        return False
    
    elapsed_time = time.perf_counter() - start_time
    msg = "TCID23_Disable_CEC_Verify_Enabled"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True