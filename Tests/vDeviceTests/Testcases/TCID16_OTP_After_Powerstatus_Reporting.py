"""
/**
 * @file TCID16_OTP_After_Powerstatus_Reporting.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID16_OTP_After_Powerstatus_Reporting
 * @details Builds a device entry through the middleware's normal CEC processing path instead
 *          of a topology dump file, then drives a one-time-play walk. Five documents are
 *          posted in order with settle windows: Process_Report_Physical_Address.yaml, whose
 *          <Report Physical Address> reaches addDevice(); Process_Set_OSD_Name.yaml and
 *          Process_Device_Vendor_ID.yaml, which fill that entry's name and vendor; then
 *          Device_CEC_Message.yaml and Device_Status.yaml. Finally
 *          org.rdk.HdmiCecSource.performOTPAction is sent over JSON-RPC.
 *
 *          The vComponent posts here are ADVISORY: _post_hdmicec() returns whether the API
 *          answered HTTP 200 and the case logs that result without consulting it, so a
 *          rejected document does not fail the case. A stricter contract would assert each
 *          post and each reply body; the criteria below describe what this module enforces,
 *          not what a stricter version could.
 *
 *          THE VERDICT IS THE OTP REQUEST ALONE: that send_curl_command returned a non-empty
 *          response. The reply body is logged, not parsed, so a refusal such as ERROR_GENERAL
 *          still passes here - position 06 is the case that asserts the OTP reply itself.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - The vComponent API is reachable and serving the emulated CEC peers; HDMICEC_CMD_BASE
 *    resolves to the commands directory this module posts from.
 *  - Authored for device-level execution and NOT executed: every criterion below states what
 *    this module asserts, not an observed result. README.txt.txt records the deferred status
 *    and the prerequisites that are unavailable.
 *
 * @dependencies
 *  - utils.py - send_curl_command, send_vcomponent_command, HDMICEC_CMD_BASE and the logging
 *    helpers.
 *  - HdmiCECSource_Curl.py - the JSON-RPC request constants this module dispatches.
 *  - SuitManager.py - the runner that registers this module and calls run_test().
 *  - vcomponent_configurations/commands/Process_Report_Physical_Address.yaml
 *  - vcomponent_configurations/commands/Process_Set_OSD_Name.yaml
 *  - vcomponent_configurations/commands/Process_Device_Vendor_ID.yaml
 *  - vcomponent_configurations/commands/Device_CEC_Message.yaml
 *  - vcomponent_configurations/commands/Device_Status.yaml
 *
 * @expected_result
 *  - The five documents are accepted by the vComponent API (logged), and the performOTPAction
 *    request produces a response.
 *
 * @pass_criteria
 *  - The performOTPAction request returns a non-empty response and run_test() returns True.
 *
 * @failure_criteria
 *  - The performOTPAction request returns nothing, so run_test() returns False. A rejected
 *    vComponent post or an unsuccessful OTP body does not fail this position.
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
