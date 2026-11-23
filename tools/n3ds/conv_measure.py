"""Measured run of usum_models (the Ultra Sun / Moon converter) on a limited PC, Windows only.

  python tools/n3ds/conv_measure.py <cpus> <label> <result folder> -- <usum_models arguments>

Puts this process (and so every program it starts) into a job object that may only use the first <cpus> logical processors,
tells the converter that is all the PC has (os.cpu_count), and writes <result folder>/result_<label>.json: wall seconds, CPU
seconds and peak memory per stage (stage lines of the converter's own protocol), and the whole run. CPU seconds do not change
when other programs share the PC, wall seconds do: report both.
"""
import ctypes
import ctypes.wintypes as W
import json
import multiprocessing
import os
import sys
import time

def main():
    cpus = int(sys.argv[1])
    label = sys.argv[2]
    outdir = sys.argv[3]
    args = sys.argv[sys.argv.index('--') + 1:]
    HERE = os.path.dirname(os.path.abspath(__file__))
    k = ctypes.windll.kernel32
    k.CreateJobObjectW.restype = W.HANDLE
    k.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
    k.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
    k.QueryInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD, ctypes.c_void_p]
    k.GetCurrentProcess.restype = W.HANDLE
    job = k.CreateJobObjectW(None, None)


    class BASIC(ctypes.Structure):
        _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64), ('PerJobUserTimeLimit', ctypes.c_int64), ('LimitFlags', W.DWORD),
                    ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', W.DWORD),
                    ('Affinity', ctypes.c_size_t), ('PriorityClass', W.DWORD), ('SchedulingClass', W.DWORD)]


    class IOC(ctypes.Structure):
        _fields_ = [(n, ctypes.c_uint64) for n in ('ro', 'wo', 'oo', 'rb', 'wb', 'ob')]


    class EXT(ctypes.Structure):
        _fields_ = [('Basic', BASIC), ('Io', IOC), ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                    ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


    class ACCT(ctypes.Structure):
        _fields_ = [('TotalUserTime', ctypes.c_int64), ('TotalKernelTime', ctypes.c_int64), ('ThisPeriodTotalUserTime', ctypes.c_int64),
                    ('ThisPeriodTotalKernelTime', ctypes.c_int64), ('TotalPageFaultCount', W.DWORD), ('TotalProcesses', W.DWORD),
                    ('ActiveProcesses', W.DWORD), ('TotalTerminatedProcesses', W.DWORD)]


    e = EXT()
    e.Basic.LimitFlags = 0x10
    e.Basic.Affinity = (1 << cpus) - 1
    print('job limits set', k.SetInformationJobObject(job, 9, ctypes.byref(e), ctypes.sizeof(e)), flush=True)
    print('assigned', k.AssignProcessToJobObject(job, k.GetCurrentProcess()), flush=True)


    def acct():
        a = ACCT()
        k.QueryInformationJobObject(job, 1, ctypes.byref(a), ctypes.sizeof(a), None)
        return (a.TotalUserTime + a.TotalKernelTime) / 1e7


    def peak():
        x = EXT()
        k.QueryInformationJobObject(job, 9, ctypes.byref(x), ctypes.sizeof(x), None)
        return x.PeakJobMemoryUsed / 2 ** 20


    os.cpu_count = lambda: cpus
    sys.path.insert(0, HERE)
    t0 = time.time()
    c0 = acct()
    stages = []
    cur = [None, t0, c0]


    def close(now, c):
        if cur[0] is not None:
            stages.append(dict(stage=cur[0], wall=round(now - cur[1], 1), cpu=round(c - cur[2], 1), peak_mb=round(peak())))


    class Out:
        def __init__(s, o):
            s.o = o
            s.b = ''

        def write(s, t):
            s.b += t
            while '\n' in s.b:
                ln, s.b = s.b.split('\n', 1)
                if ln.startswith('stage '):
                    now = time.time()
                    c = acct()
                    close(now, c)
                    cur[:] = [ln.split()[1], now, c]
                if not ln.startswith('progress'):
                    s.o.write('%7.0f %s\n' % (time.time() - t0, ln))
                    s.o.flush()
            return len(t)

        def flush(s):
            s.o.flush()


    real = sys.stdout
    sys.stdout = Out(real)
    multiprocessing.freeze_support()
    sys.argv = ['usum_models.py'] + args
    import usum_models
    rc = 0
    try:
        usum_models.main()
    except SystemExit as ex:
        rc = ex.code
    now = time.time()
    c = acct()
    close(now, c)
    res = dict(label=label, cpus=cpus, rc=rc, total_wall=round(now - t0, 1), total_cpu=round(c - c0, 1), peak_job_mb=round(peak()), stages=stages)
    os.makedirs(outdir, exist_ok=True)
    json.dump(res, open(os.path.join(outdir, 'result_%s.json' % label), 'w'), indent=1)
    print(json.dumps(res, indent=1), file=real, flush=True)


if __name__ == '__main__':
    main()
