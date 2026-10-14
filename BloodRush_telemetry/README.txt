BloodRush_telemetry — motion-platform cart bridge

Requires Python 3 on PATH.

  run.bat        listen for live cart UDP from BloodRush (2D / PC-VR) on :25151
                 and serve TCP telemetry on 127.0.0.1:15151
  run_demo.bat   fake ride (no game) for testing chair software

Usage:
  1. Start run.bat (or run_telemetry.bat from the release root)
  2. Start BloodRush 2D or PC-VR
  3. Point your motion software at 127.0.0.1:15151

The rebuilt desktop\shadps4.exe / pc-vr\shadps4.exe already emit cart UDP.
Disable with environment variable: SHADPS4_ROB_MOTION_TELEMETRY=0
