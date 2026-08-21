"""
/**
 * @file TCID20_Standby_OTP_Userdef_CEC.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID20_Standby_OTP_Userdef_CEC
 * @details Combines a seeded device, a user-defined opcode and a standby/wake-up pair. The
 *          triplet Process_Report_Physical_Address.yaml, Process_Set_OSD_Name.yaml and
 *          Process_Device_Vendor_ID.yaml seeds an entry through the middleware's own CEC
 *          processing path; Device_CEC_Message_Userdef.yaml carries a user-defined opcode and
 *          Device_Status.yaml follows it; org.rdk.HdmiCecSource.sendStandbyMessage is sent;
 *          Device_Standby_Emulation.yaml reports the peer as standing by; performOTPAction is
 *          sent; and Device_Image_View_On.yaml emulates the resulting power-on.
 *
 *          The vComponent posts here are ADVISORY: _post_hdmicec() returns whether the API
 *          answered HTTP 200 and the case logs that result without consulting it, so a
 *          rejected document does not fail the case. A stricter contract would assert each
 *          post and each reply body; the criteria below describe what this module enforces,
 *          not what a stricter version could.
 *
 *          THE VERDICT IS THE TWO JSON-RPC REQUESTS: each of sendStandbyMessage and
 *          performOTPAction must return a non-empty response. The user-defined opcode's effect
 *          is not observable through this transport, so it is exercised rather than asserted.
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
 *  - vcomponent_configurations/commands/Device_CEC_Message_Userdef.yaml
 *  - vcomponent_configurations/commands/Device_Status.yaml
 *  - vcomponent_configurations/commands/Device_Standby_Emulation.yaml
 *  - vcomponent_configurations/commands/Device_Image_View_On.yaml
 *
 * @expected_result
 *  - The seven documents are accepted (logged) and both JSON-RPC requests produce a response.
 *
 * @pass_criteria
 *  - sendStandbyMessage and performOTPAction each return a non-empty response and run_test()
 *    returns True.
 *
 * @failure_criteria
 *  - Either JSON-RPC request returns nothing, so run_test() returns False. A rejected
 *    vComponent post does not fail this position.
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

    log_success("Reporting power status through control pane - vComponent")
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
    _post_hdmicec("Device_CEC_Message_Userdef.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Status.yaml")

    time.sleep(3)
    log_info("Sending the curl command to make the device standby")
    curl_response = send_curl_command(
        HdmiCecSourceApis.send_standby_message
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    time.sleep(3)
    log_success("Reporting standby emulation through control pane - vComponent")
    _post_hdmicec("Device_Standby_Emulation.yaml")

    time.sleep(2)
    curl_response = send_curl_command(
        HdmiCecSourceApis.perform_otp_action
    )

    if not curl_response:
        log_error("✖ performOTPAction curl command not sent")
        return False

    log_success("Reporting power-on emulation through control pane - vComponent")
    time.sleep(3)
    _post_hdmicec("Device_Image_View_On.yaml")

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")
    log_success("All commands executed successfully")

    elapsed_time = time.perf_counter() - start_time
    msg = "Standby OTP userdef CEC passed"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
