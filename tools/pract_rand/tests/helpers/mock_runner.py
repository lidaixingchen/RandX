#!/usr/bin/env python3
"""测试专用子进程模拟脚本."""

import os
import signal
import sys
import time

def main():
    mode = os.environ.get("MOCK_MODE", "normal")

    if mode == "silent":
        time.sleep(10)
    elif mode == "banner_silent":
        sys.stdout.write("RNG_test using PractRand version 0.95\n")
        sys.stdout.flush()
        time.sleep(10)
    elif mode == "no_newline_silent":
        sys.stdout.write("partial line without newline")
        sys.stdout.flush()
        time.sleep(10)
    elif mode == "crash":
        sys.exit(1)
    elif mode == "crash_signal":
        if hasattr(signal, "SIGSEGV") and os.name != "nt":
            os.kill(os.getpid(), signal.SIGSEGV)
        else:
            sys.exit(-11)
    elif mode == "pass_exit_0":
        sys.stdout.write("RNG_test using PractRand version 0.95\n")
        sys.stdout.write("RNG = RNG_stdin64, seed = 0x9e3779b97f4a7c15\n")
        sys.stdout.write("test set = core, folding = standard (64 bit)\n\n")
        sys.stdout.write("rng=RNG_stdin64, seed=0x9e3779b97f4a7c15\n")
        sys.stdout.write("length= 32 megabytes (2^25 bytes), time= 0.1 seconds\n")
        sys.stdout.write("  no anomalies in 126 test result(s)\n")
        sys.stdout.flush()
        sys.exit(0)
    elif mode == "checkpoint_sequence":
        sys.stdout.write("RNG_test using PractRand version 0.95\n")
        sys.stdout.write("length= 1 megabyte (2^20 bytes), time= 0.1 seconds\n")
        sys.stdout.write("  Test Name: BCFN(2+0,13-0,T) ... mildly suspicious\n")
        sys.stdout.write("  ...and 125 other test result(s)\n")
        sys.stdout.write("length= 2 megabytes (2^21 bytes), time= 0.1 seconds\n")
        if os.environ.get("MOCK_CHECKPOINT_MODE") == "same_name":
            sys.stdout.write("  Test Name: BCFN(2+0,13-0,T) ... mildly suspicious\n")
            sys.stdout.write("  ...and 125 other test result(s)\n")
        elif os.environ.get("MOCK_CHECKPOINT_MODE") == "tail_suspicious":
            sys.stdout.write("  Test Name: BCFN(2+0,13-0,T) ... mildly suspicious\n")
            sys.stdout.flush()
            time.sleep(10)
        else:
            sys.stdout.write("  no anomalies in 126 test result(s)\n")
        sys.stdout.flush()
        if os.environ.get("MOCK_CHECKPOINT_MODE") == "tail_suspicious":
            time.sleep(10)
        sys.exit(0)
    elif mode == "generator_infinite":
        try:
            chunk = b"A" * 4096
            while True:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                time.sleep(0.001)
        except BaseException:
            os._exit(0)
    elif mode == "generator_stubborn":
        # 持续循环且捕获所有异常继续循环，用于测试 supervisor_cleanup
        while True:
            try:
                sys.stdout.buffer.write(b"A" * 1024)
                sys.stdout.buffer.flush()
            except BaseException:
                pass
            time.sleep(0.01)
    elif mode == "ignore_sigterm":
        if hasattr(signal, "SIGTERM"):
            try:
                signal.signal(signal.SIGTERM, signal.SIG_IGN)
            except Exception:
                pass
        time.sleep(10)
    else:
        try:
            data = sys.stdin.read()
            sys.stdout.write(data)
        except Exception:
            pass

if __name__ == "__main__":
    main()
