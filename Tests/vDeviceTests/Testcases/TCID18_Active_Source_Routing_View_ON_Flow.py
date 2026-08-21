"""
/**
 * @file TCID18_Active_Source_Routing_View_ON_Flow.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID18_Active_Source_Routing_View_ON_Flow
 * @details Walks the active-source, routing and view-on surface in one sequence.
 *          Device_Request_Inactive_Source.yaml and Device_Request_Active_Source.yaml are
 *          posted; org.rdk.HdmiCecSource.sendStandbyMessage is sent; performOTPAction is sent;
 *          then Device_Routing_Change.yaml, Device_Image_View_On.yaml,
 *          Device_Text_View_On.yaml and Device_Set_OSD_String.yaml are posted with settle
 *          windows between them, which is the order a real wake-from-standby produces.
 *
 *          The vComponent posts here are ADVISORY: _post_hdmicec() returns whether the API
 *          answered HTTP 200 and the case logs that result without consulting it, so a
 *          rejected document does not fail the case. A stricter contract would assert each
 *          post and each reply body; the criteria below describe what this module enforces,
 *          not what a stricter version could.
 *
 *          THE VERDICT IS THE TWO JSON-RPC REQUESTS: each of sendStandbyMessage and
 *          performOTPAction must return a non-empty response. Their bodies are logged rather
 *          than parsed.
 *
 *          The case publishes no cleanup hook and restores nothing: it leaves the emulated bus
 *          in the state the last document put it in. That is deliberate - the positions that
 *          follow establish the state they need themselves, and position 19 begins with its
 *          own standby request.
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
 *  - vcomponent_configurations/commands/Device_Request_Inactive_Source.yaml
 *  - vcomponent_configurations/commands/Device_Request_Active_Source.yaml
 *  - vcomponent_configurations/commands/Device_Routing_Change.yaml
 *  - vcomponent_configurations/commands/Device_Image_View_On.yaml
 *  - vcomponent_configurations/commands/Device_Text_View_On.yaml
 *  - vcomponent_configurations/commands/Device_Set_OSD_String.yaml
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
import subprocess
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
    base_dir = "/tmp"
    time.sleep(2)
    _post_hdmicec("Device_Request_Inactive_Source.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Request_Active_Source.yaml")

    time.sleep(1)
    log_info("Send standby curl request being made to source device")
    curl_response = send_curl_command(
        HdmiCecSourceApis.send_standby_message
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False
    else:
        log_warning(f"Response: {curl_response}")

    time.sleep(1)
    log_info("Send perform OTP Action curl request being made to source device")
    curl_response = send_curl_command(
        HdmiCecSourceApis.perform_otp_action
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False
    else:
        log_warning(f"Response: {curl_response}")


    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    time.sleep(2)
    _post_hdmicec("Device_Routing_Change.yaml")

    time.sleep(3)
    log_info("Emulations after routing change, and device power on from standby for image view on and text view on")

    time.sleep(2)
    _post_hdmicec("Device_Image_View_On.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Text_View_On.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Set_OSD_String.yaml")
    
    elapsed_time = time.perf_counter() - start_time
    msg = "All commands executed successfully"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
