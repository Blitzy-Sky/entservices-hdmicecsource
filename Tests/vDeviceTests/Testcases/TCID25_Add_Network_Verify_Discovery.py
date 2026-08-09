"""
/**
 * @file TCID25_Add_Network_Verify_Discovery.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID25_Add_Network_Verify_Discovery
 * @details Checks that adding peers to the emulated network does not shrink the published
 *          inventory, with a legacy escape hatch for targets that still carry the original
 *          event script. _resolve_send_events_script() searches four tiers in order: the
 *          SEND_EVENTS_SCRIPT environment override; three fixed candidates (a sendEvents.sh
 *          five levels above this directory, one beside this module, and /tmp/sendEvents.sh);
 *          and an OLD_TESTCASE_RDKE/rdkservices/L2HalMock/sendEvents.sh beneath any ancestor
 *          directory. The first existing candidate is run with /bin/bash.
 *
 *          SCRIPT HANDLING IS ASYMMETRIC ON PURPOSE: a non-zero exit or an exception from the
 *          script FAILS the case, while the script being absent is only a warning and the
 *          vComponent path is used instead - so a target without the legacy asset is
 *          supported, but a target whose script breaks is reported.
 *
 *          org.rdk.HdmiCecSource.getDeviceList is read for a baseline;
 *          Device_Config_Add_Network.yaml and Device_Status.yaml are posted and BOTH ARE
 *          REQUIRED; then getDeviceList is read again.
 *
 *          THE VERDICT IS MONOTONIC RATHER THAN EXACT: the final reply must carry no "error"
 *          member, result.success exactly True, and an integer numberofdevices which, when a
 *          baseline count was obtained, must be greater than or equal to it. A count that grew
 *          is accepted, a count that shrank is not, and a missing baseline reduces the check
 *          to the integer requirement rather than failing the case.
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
 *  - vcomponent_configurations/commands/Device_Config_Add_Network.yaml
 *  - vcomponent_configurations/commands/Device_Status.yaml
 *  - Optionally a legacy sendEvents.sh, located through SEND_EVENTS_SCRIPT or one of the
 *    fallback paths; its absence is supported.
 *
 * @expected_result
 *  - The legacy script, if present, exits zero; both documents are accepted; and the final
 *    getDeviceList reports success with a device count no lower than the baseline.
 *
 * @pass_criteria
 *  - No script failure, both posts accepted, a non-empty final reply with no error member,
 *    success True and an integer count at least the baseline, and run_test() returns True.
 *
 * @failure_criteria
 *  - The legacy script exits non-zero or raises; either post is rejected; the final request
 *    returns nothing; the reply carries an error member; success is not True; the count is not
 *    an int or is lower than the baseline; or the reply does not parse.
 */
"""


import json
import os
import subprocess
import time
import os
from pathlib import Path
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


def _resolve_send_events_script():
    # Priority 1: explicit override from environment.
    env_path = os.environ.get("SEND_EVENTS_SCRIPT", "").strip()
    if env_path and Path(env_path).is_file():
        return env_path

    # Priority 2: common legacy locations.
    base = Path(__file__).resolve().parent
    candidates = [
        base / "../../../../../sendEvents.sh",
        base / "sendEvents.sh",
        Path("/tmp/sendEvents.sh"),
    ]

    # Priority 3: discover classic legacy location from any ancestor.
    for ancestor in [base, *base.parents]:
        candidates.append(
            ancestor / "OLD_TESTCASE_RDKE/rdkservices/L2HalMock/sendEvents.sh"
        )

    for c in candidates:
        p = c.resolve() if not c.is_absolute() else c
        if p.is_file():
            return str(p)

    return ""


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: sending events simulation.
    before = send_curl_command(HdmiCecSourceApis.get_device_list)

    # Try legacy-equivalent event script first when available.
    send_events_script = _resolve_send_events_script()
    if send_events_script:
        log_info(f"Executing legacy sendEvents script: {send_events_script}")
        try:
            result = subprocess.run(
                ["/bin/bash", send_events_script],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            if result.returncode != 0:
                log_error(f"✖ sendEvents script failed: {result.stderr.strip()}")
                return False
            log_success("✔ sendEvents script executed successfully")
        except Exception as exc:
            log_error(f"✖ sendEvents script execution exception: {exc}")
            return False
    else:
        log_warning("sendEvents.sh not found; using vComponent emulation fallback path")

    ok1 = _post_hdmicec("Device_Config_Add_Network.yaml")
    time.sleep(1)
    ok2 = _post_hdmicec("Device_Status.yaml")
    time.sleep(1)

    if not (ok1 and ok2):
        log_error("✖ required vComponent emulation posts failed")
        return False

    response = send_curl_command(HdmiCecSourceApis.get_device_list)
    if not response:
        log_error("✖ getDeviceList curl command not sent")
        return False

    log_warning(f"Response: {response}")
    try:
        before_count = None
        if before:
            bbody = json.loads(before)
            before_count = bbody.get("result", {}).get("numberofdevices")

        body = json.loads(response)
        result = body.get("result", {})
        after_count = result.get("numberofdevices")
        count_valid = isinstance(after_count, int)
        if isinstance(before_count, int):
            count_valid = count_valid and after_count >= before_count

        if "error" not in body and result.get("success") is True and count_valid:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID25_Add_Network_Verify_Discovery Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except json.JSONDecodeError:
        pass

    log_error(f"TCID25_Add_Network_Verify_Discovery Failed ❌")
    return False
