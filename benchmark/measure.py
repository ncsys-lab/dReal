#!/usr/bin/env python3
"""Run one solver command on the E-cores under a CPU-seconds limit, and record what it used.

Usage: measure.py CPU_CAP_S PREFIX CMD [ARG...]
  Runs CMD in the background band (PRIO_DARWIN_BG, what `taskpolicy -b` sets: E-cores only,
  below every nice level) with RLIMIT_CPU = CPU_CAP_S, so the limit is CPU time, not wall clock, and machine load can't
  turn into timeouts. stdout/stderr are inherited (the caller redirects them). Writes
  PREFIX.rusage (JSON: cpu_s, wall_s, p_share, instructions, cycles, max_footprint_kb) from
  proc_pid_rusage(RUSAGE_INFO_V6), read before the child is reaped, and PREFIX.exit (the
  shell-style status: 128+N for signal N, so SIGXCPU at the cap is 152, SIGKILL 137).

macOS cannot pin a process to P-cores (taskpolicy only clamps down; THREAD_AFFINITY_POLICY is
an L2-sharing hint), but the background band keeps it on E-cores. Measured 2026-10-07 on the M4
Max: P-core share 0.00 quiet and under 12 competing processes; an E-core takes 2.2-5.5x the CPU
time of a P-core for the same dReal run. p_share is recorded so parse_results.py can check the pin.
"""
import ctypes
import json
import os
import resource
import sys

_FIELDS = (
    "user_time system_time pkg_idle_wkups interrupt_wkups pageins wired_size resident_size "
    "phys_footprint proc_start_abstime proc_exit_abstime child_user_time child_system_time "
    "child_pkg_idle_wkups child_interrupt_wkups child_pageins child_elapsed_abstime "
    "diskio_bytesread diskio_byteswritten cpu_time_qos_default cpu_time_qos_maintenance "
    "cpu_time_qos_background cpu_time_qos_utility cpu_time_qos_legacy "
    "cpu_time_qos_user_initiated cpu_time_qos_user_interactive billed_system_time "
    "serviced_system_time logical_writes lifetime_max_phys_footprint instructions cycles "
    "billed_energy serviced_energy interval_max_phys_footprint runnable_time flags user_ptime "
    "system_ptime pinstructions pcycles energy_nj penergy_nj secure_time_in_system "
    "secure_ptime_in_system neural_footprint lifetime_max_neural_footprint "
    "interval_max_neural_footprint conclave_footprint page_wait_time_mach page_cache_hits").split()


class RusageInfoV6(ctypes.Structure):   # <sys/resource.h> struct rusage_info_v6
    _fields_ = ([("uuid", ctypes.c_uint8 * 16)] + [(f, ctypes.c_uint64) for f in _FIELDS]
                + [("reserved", ctypes.c_uint64 * 6)])


class MachTimebase(ctypes.Structure):
    _fields_ = [("numer", ctypes.c_uint32), ("denom", ctypes.c_uint32)]


_libc = ctypes.CDLL("/usr/lib/libSystem.B.dylib")
_RUSAGE_INFO_V6 = 6
_PRIO_DARWIN_PROCESS, _PRIO_DARWIN_BG = 4, 0x1000   # <sys/resource.h>


def _ticks_to_s(ticks: int) -> float:
    tb = MachTimebase()
    _libc.mach_timebase_info(ctypes.byref(tb))
    return ticks * tb.numer / tb.denom / 1e9


def main() -> None:
    cap_s, prefix, cmd = int(sys.argv[1]), sys.argv[2], sys.argv[3:]
    # Enter the background band before forking so the child spends its whole life on E-cores
    # (set in the child, its first instructions ran on P-cores: p_share 0.05 on short runs).
    os.setpriority(_PRIO_DARWIN_PROCESS, 0, _PRIO_DARWIN_BG)
    pid = os.fork()
    if pid == 0:
        resource.setrlimit(resource.RLIMIT_CPU, (cap_s, cap_s + 5))
        os.execvp(cmd[0], cmd)
    os.waitid(os.P_PID, pid, os.WEXITED | os.WNOWAIT)   # exited, not yet reaped
    ru = RusageInfoV6()
    if _libc.proc_pid_rusage(pid, _RUSAGE_INFO_V6, ctypes.byref(ru)) != 0:
        raise OSError(ctypes.get_errno(), f"proc_pid_rusage({pid}) failed")
    rc = os.waitstatus_to_exitcode(os.waitpid(pid, 0)[1])
    cpu = ru.user_time + ru.system_time
    with open(prefix + ".rusage", "w") as f:
        json.dump({"cpu_s": _ticks_to_s(cpu),
                   "wall_s": _ticks_to_s(ru.proc_exit_abstime - ru.proc_start_abstime),
                   "p_share": (ru.user_ptime + ru.system_ptime) / cpu,
                   "instructions": ru.instructions, "cycles": ru.cycles,
                   "max_footprint_kb": ru.lifetime_max_phys_footprint // 1024}, f)
    with open(prefix + ".exit", "w") as f:
        f.write(f"{128 - rc if rc < 0 else rc}\n")


if __name__ == "__main__":
    main()
