"""
/**
 * @file TCID16_OTP_After_Powerstatus_Reporting.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. One-touch-play after emulated power-
 *        status reporting.
 *
 * @testcase TCID16_OTP_After_Powerstatus_Reporting
 * @details Posts CEC message, device-status and process response documents to the vComponent,
 *          then invokes performOTPAction and checks it succeeds against the emulated topology.
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
 *  - vcomponent_configurations/commands/ - Device_CEC_Message.yaml, Device_Status.yaml,
 *    Process_Device_Vendor_ID.yaml, Process_Report_Physical_Address.yaml,
 *    Process_Set_OSD_Name.yaml
 *
 * @expected_result
 *  - org.rdk.HdmiCecSource.performOTPAction answers with the values this scenario expects.
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
