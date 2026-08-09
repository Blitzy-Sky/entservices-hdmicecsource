"""
/**
 * @file TCID24_Standby_Userdef_Busstatus.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID24_Standby_Userdef_Busstatus
 * @details Posts Device_CEC_Message_Userdef.yaml and Device_Bus_Status.yaml - BOTH REQUIRED,
 *          the case fails if either is not accepted - and then sends
 *          org.rdk.HdmiCecSource.sendStandbyMessage and asserts the reply's NESTED fields:
 *          result must be a dict and result.success exactly True.
 *
 *          Reading the result body rather than comparing a whole envelope is the difference
 *          from position 01: the id and jsonrpc members are not part of this case's contract,
 *          so the standby request is asserted after emulation without pinning the envelope
 *          shape twice.
 *
 *          A JSONDecodeError is swallowed and falls through to the failure log, so an
 *          unparseable reply fails the case rather than raising out of run_test().
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
 *  - vcomponent_configurations/commands/Device_CEC_Message_Userdef.yaml
 *  - vcomponent_configurations/commands/Device_Bus_Status.yaml
 *
 * @expected_result
 *  - Both documents are accepted and the standby reply carries result.success true.
 *
 * @pass_criteria
 *  - Both posts are accepted, the standby response is non-empty, result is a dict whose
 *    success is True, and run_test() returns True.
 *
 * @failure_criteria
 *  - Either post is rejected, the standby request returns nothing, result is missing or not a
 *    dict, success is not exactly True, or the reply does not parse.
 */
"""


import json
import time
import os
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
    http_code, body = send_vcomponent_command(f"{HDMICEC_CMD_BASE}/{yaml_file}")
    log_info(f"  vComponent POST {yaml_file}: HTTP {http_code}  {body}")
    return http_code == 200


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: abort/feature-abort process emulation.
    ok1 = _post_hdmicec("Device_CEC_Message_Userdef.yaml")
    time.sleep(1)
    ok2 = _post_hdmicec("Device_Bus_Status.yaml")
    time.sleep(1)

    if not (ok1 and ok2):
        log_error("✖ required vComponent emulation posts failed")
        return False

    response = send_curl_command(HdmiCecSourceApis.send_standby_message)
    if not response:
        log_error("✖ standby curl command not sent")
        return False

    log_warning(f"Response: {response}")
    try:
        body = json.loads(response)
        ok = isinstance(body.get("result"), dict) and body["result"].get("success") is True
        if ok:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID24_Standby_Userdef_Busstatus Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except json.JSONDecodeError:
        pass

    log_error(f"TCID24_Standby_Userdef_Busstatus Failed ❌")
    return False
