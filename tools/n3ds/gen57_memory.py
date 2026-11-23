"""How many worker processes the converter may start: what the processors allow, cut down so that everything fits in the PC's free memory
(a PC with little memory must not swap: every worker is a Python process holding a dump reader, the XD disc and one model's meshes).

  GEN57_MEMORY_MB=<n>   pretend n MB are free (tests: an 8 GB PC)
  GEN57_WORKER_MB=<n>   test override of the per-worker figure
"""
import os



WORKER_MB = 1000
MAIN_MB = 1200
KEEP_FREE_MB = 1500


def cpu_count():
    """The processors this program may really use (the affinity mask / a job's limit, not every processor of the PC)."""
    try:
        if hasattr(os, 'sched_getaffinity'):
            return max(1, len(os.sched_getaffinity(0)))
        if os.name == 'nt':
            import ctypes
            mask, sysmask = ctypes.c_size_t(), ctypes.c_size_t()
            h = ctypes.windll.kernel32.GetCurrentProcess()
            if ctypes.windll.kernel32.GetProcessAffinityMask(ctypes.c_void_p(h), ctypes.byref(mask), ctypes.byref(sysmask)):
                return max(1, bin(mask.value).count('1'))
    except Exception:
        pass
    return os.cpu_count() or 2


def available_memory_mb():
    """Free memory in MB the system could give a new program (None when it cannot be told)."""
    v = os.environ.get('GEN57_MEMORY_MB')
    if v and v.isdigit():
        return int(v)
    try:
        if os.name == 'nt':
            import ctypes

            class MS(ctypes.Structure):
                _fields_ = [('dwLength', ctypes.c_ulong), ('dwMemoryLoad', ctypes.c_ulong), ('ullTotalPhys', ctypes.c_ulonglong),
                            ('ullAvailPhys', ctypes.c_ulonglong), ('ullTotalPageFile', ctypes.c_ulonglong), ('ullAvailPageFile', ctypes.c_ulonglong),
                            ('ullTotalVirtual', ctypes.c_ulonglong), ('ullAvailVirtual', ctypes.c_ulonglong), ('ullAvailExtendedVirtual', ctypes.c_ulonglong)]
            ms = MS()
            ms.dwLength = ctypes.sizeof(ms)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(ms))
            return int(ms.ullAvailPhys // (1 << 20))
        if os.path.exists('/proc/meminfo'):
            for ln in open('/proc/meminfo'):
                if ln.startswith('MemAvailable:'):
                    return int(ln.split()[1]) // 1024
        import subprocess
        out = subprocess.run(['vm_stat'], capture_output=True, text=True, timeout=10).stdout
        page, pages = 4096, 0
        for ln in out.splitlines():
            if 'page size of' in ln:
                page = int(ln.split('page size of')[1].split()[0])
            if ln.startswith(('Pages free', 'Pages inactive', 'Pages speculative')):
                pages += int(ln.split(':')[1].strip().rstrip('.'))
        return pages * page // (1 << 20) if pages else None
    except Exception:
        return None


def workers_for(cpu_cap, log=None):
    """`cpu_cap` (what the processors allow), cut down to what the free memory holds (at least 1)."""
    avail = available_memory_mb()
    per = int(os.environ.get('GEN57_WORKER_MB') or WORKER_MB)
    n = cpu_cap
    if avail is not None:
        n = max(1, min(cpu_cap, (avail - KEEP_FREE_MB - MAIN_MB) // per))
    if log:
        log('workers: %d (the processors allow %d, free memory %s MB)' % (n, cpu_cap, avail))
    return int(n)
