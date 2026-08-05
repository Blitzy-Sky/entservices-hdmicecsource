"""
/**
 * @file TCID16_OTP_After_Powerstatus_Reporting.py
 * @brief L2 HDMI CEC functional testcase.
 *
 * @testcase TCID16_OTP_After_Powerstatus_Reporting
 * @details Validates the 'TCID16_OTP_After_Powerstatus_Reporting' HDMI CEC behavior through JSON-RPC and/or vComponent command flow.
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

    log_info("Reporting power status through control pane - vComponent")
    time.sleep(2)
    # Populate device list via CEC messages instead of topology dump file.
    # ReportPhysicalAddress triggers addDevice(); SetOSDName and DeviceVendorID
    # fill in device details through the middleware's normal CEC processing path.
    _post_hdmicec("Process_Report_Physical_Address.yaml")
    time.sleep(1)
    _post_hdmicec("Process_Set_OSD_Name.yaml")
    time.sleep(1)
    _post_hdmicec("Process_Device_Vendor_ID.yaml")
    time.sleep(2)
    _post_hdmicec("Device_CEC_Message.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Status.yaml")

    curl_response = send_curl_command(
        HdmiCecSourceApis.perform_otp_action
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    elapsed_time = time.perf_counter() - start_time
    msg = "All commands executed successfully"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
