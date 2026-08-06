"""
/**
 * @file TCID24_Standby_Userdef_Busstatus.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Standby over a user-defined CEC
 *        exchange with an altered bus status.
 *
 * @testcase TCID24_Standby_Userdef_Busstatus
 * @details Posts bus-status and user-defined CEC message documents to the vComponent and checks
 *          sendStandbyMessage behaves as expected against that bus state.
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
 *  - vcomponent_configurations/commands/ - Device_Bus_Status.yaml,
 *    Device_CEC_Message_Userdef.yaml
 *
 * @expected_result
 *  - org.rdk.HdmiCecSource.sendStandbyMessage answers with the values this scenario expects.
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
