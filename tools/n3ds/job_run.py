"""Runs any command inside a Windows job object limited to the first <cpus> logical processors and reports wall seconds, CPU seconds and peak memory
(all processes together) as JSON: python job_run.py <cpus> <result.json> -- <command> [args...]. conv_measure.py does the same for the converter's
own stage lines; this one is for a whole scripted launcher run."""
import ctypes
import ctypes.wintypes as W
import json
import subprocess
import sys
import time

cpus, out = int(sys.argv[1]), sys.argv[2]
cmd = sys.argv[sys.argv.index('--') + 1:]
k = ctypes.windll.kernel32
k.CreateJobObjectW.restype = W.HANDLE
k.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
k.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
k.QueryInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD, ctypes.c_void_p]
k.GetCurrentProcess.restype = W.HANDLE
job = k.CreateJobObjectW(None, None)


class BASIC(ctypes.Structure):
    _fields_ = [('a', ctypes.c_int64), ('b', ctypes.c_int64), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t), ('e', W.DWORD),
                ('Affinity', ctypes.c_size_t), ('f', W.DWORD), ('g', W.DWORD)]


class IOC(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint64) for n in ('ro', 'wo', 'oo', 'rb', 'wb', 'ob')]


class EXT(ctypes.Structure):
    _fields_ = [('Basic', BASIC), ('Io', IOC), ('p', ctypes.c_size_t), ('j', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


class ACCT(ctypes.Structure):
    _fields_ = [('User', ctypes.c_int64), ('Kernel', ctypes.c_int64), ('x', ctypes.c_int64), ('y', ctypes.c_int64), ('pf', W.DWORD), ('tp', W.DWORD), ('ap', W.DWORD), ('tt', W.DWORD)]


e = EXT()
e.Basic.LimitFlags = 0x10
e.Basic.Affinity = (1 << cpus) - 1
k.SetInformationJobObject(job, 9, ctypes.byref(e), ctypes.sizeof(e))
k.AssignProcessToJobObject(job, k.GetCurrentProcess())
t0 = time.time()
rc = subprocess.call(cmd)
wall = time.time() - t0
a = ACCT()
k.QueryInformationJobObject(job, 1, ctypes.byref(a), ctypes.sizeof(a), None)
x = EXT()
k.QueryInformationJobObject(job, 9, ctypes.byref(x), ctypes.sizeof(x), None)
res = dict(cpus=cpus, rc=rc, wall=round(wall, 1), cpu=round((a.User + a.Kernel) / 1e7, 1), peak_mb=round(x.PeakJobMemoryUsed / 2 ** 20))
json.dump(res, open(out, 'w'), indent=1)
print(json.dumps(res))
sys.exit(rc)
