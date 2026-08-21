"""
/**
 * @file TCID01_Send_Standby_Message.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID01_Send_Standby_Message
 * @details Sends org.rdk.HdmiCecSource.sendStandbyMessage, which takes no parameters, by
 *          handing HdmiCECSource_Curl.send_standby_message to send_curl_command; the request
 *          is referenced rather than rebuilt so the transport contract stays in one place. The
 *          reply is compared as a WHOLE ENVELOPE, so the jsonrpc member and the request id 42
 *          are part of the contract and not only the result body.
 *
 *          The <Standby> frame the plugin broadcasts as a consequence travels on the CEC bus,
 *          which this transport cannot observe, so the acknowledgement is all that is
 *          asserted. The case must not be read as evidence that the broadcast reached a peer.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - CEC is enabled. The plugin defaults cecEnabled to true when its persisted settings file
 *    carries no such label, and positions 03 and 09 both leave it enabled.
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
 *  - Empty response, any difference anywhere in the envelope including the id or the jsonrpc
 *    member, a JSONDecodeError, or run_test() returns False.
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

    log_info("Executing the curl command send standby message")

    curl_response = send_curl_command(
        HdmiCecSourceApis.send_standby_message
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        if json.loads(curl_response) == expected_output_response:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID01_Send_Standby_Message Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
        else:
            log_error(f"TCID01_Send_Standby_Message Failed ❌")
            return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID01_Send_Standby_Message Failed ❌")
        return False
