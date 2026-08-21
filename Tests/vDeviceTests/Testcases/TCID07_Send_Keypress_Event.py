"""
/**
 * @file TCID07_Send_Keypress_Event.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID07_Send_Keypress_Event
 * @details Sends org.rdk.HdmiCecSource.sendKeyPressEvent with params logicalAddress 0 and
 *          keyCode 65. Address 0 is the television, which is the destination a source device
 *          actually sends user-control frames to, and 65 is the code carried by the request
 *          constant in HdmiCECSource_Curl.py. The reply is compared as a WHOLE ENVELOPE, so
 *          the jsonrpc member and the request id 42 are part of the contract and not only the
 *          result body.
 *
 *          The <User Control Pressed> frame the plugin emits travels on the CEC bus and is not
 *          observable through this transport, so only the acknowledgement is asserted.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - CEC is enabled, so the plugin has an open connection to send the frame on.
 *  - Authored for device-level execution and NOT executed: every criterion below states what
 *    this module asserts, not an observed result. README.txt.txt records the deferred status
 *    and the prerequisites that are unavailable.
 *
 * @dependencies
 *  - utils.py - send_curl_command and the logging helpers.
 *  - HdmiCECSource_Curl.py - the JSON-RPC request constants this module dispatches.
 *  - SuitManager.py - the runner that registers this module and calls run_test().
 *
 * @expected_result
 *  - The reply parses as JSON and equals {"jsonrpc":"2.0","id":42,"result":{"success":true}}
 *    exactly.
 *
 * @pass_criteria
 *  - The response is non-empty, the parsed reply equals that envelope exactly, and run_test()
 *    returns True.
 *
 * @failure_criteria
 *  - Empty response, any envelope difference, a JSONDecodeError, or run_test() returns False.
 */
"""


import time
import os


import json
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

    expected_output_response = {
        "jsonrpc": "2.0",
        "id": 42,
        "result": {
            "success": True
        }
    }

    log_info("Executing the curl command send key press event")

    curl_response = send_curl_command(
        HdmiCecSourceApis.send_key_press_event
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        if json.loads(curl_response) == expected_output_response:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID07_Send_Keypress_Event Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
        else:
            log_error(f"TCID07_Send_Keypress_Event Failed ❌")
            return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID07_Send_Keypress_Event Failed ❌")
        return False
