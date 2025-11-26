# **JUTILS**

 
_Various command line utilities to make a sysad's life easier._  


## KERNMEM:

Computes a practical, real-world estimate of total kernel memory usage using
/proc
It gathers:

### Kernel code
### Kernel data
### Kernel bss
### Slab (total, reclaimable, unreclaimable)
### Page tables
### Vmalloc allocations
### Total module memory (lsmod equivalent parsing /proc/modules)

And prints subtotals + grand total and workd on any linux kernel that supports
/proc/meminfo and /proc/modules.

Esitmate also includes... 

real kernel image memory (text, data, bss), slab allocations, page tables, 
module memory and vmalloc area usage.

The program will compensate for kernels that do not expose the fields KernelCode,
KernelData and KernelBss.  In this case it will try to read from /proc/kallsyms or
System.map in case kernel address space layout randomization cock-blocks ya. ;-)



## CLIPIT - OSC-52 clipboard copier.

Is a fully standalone, dependency-free OSC-52 clipboard utility written in pure C, 
including its own Base64 encoder.  It reads any size input (configurable), 
Base64-encodes it, wraps it in the OSC-52 escape sequence, and writes to stdout...
copying directly into your GUI clipboard on any terminal that supports OSC-52.

This utility compiles on any Linux, BSD, macOS, etc.

Clipit will work in the following terminals

* Gnome
* xterm
* iTerm2
* Konsole
* Alacritty
* most wayland terminals
* tmux (if set -g allow-passthrough on)
* SSH remote sessions (if forwarding is allowed)

As long as your terminal supports pasting large payloads, you're good.


## TOOLCHAIN ENV - a cross-compile toolchain set-up environment.

This shell script will provide all the environment variables with paths
for your toolchian of choice for all your cross-compiiling needs.
This tool is focused on the defaults used in crosstool-ng for locations
of compilers and toolchains.

Simply source the script (. /path/to/toolchain-env.sh) and tell it where
to look...

  export TOOLCHAIN_DIRS="$HOME/x-tools:/opt:/usr/local:/opt/toolchains"

from your shell you will have a "tc" function to manage cross-compiler 
environments:

*   tc list              - list discovered toolchains (triples and roots)
*   tc use <triple|path> - activate toolchain by triple or by path to its root/bin/<triple>-gcc
*   tc which             - print current CC/CXX/CROSS_COMPILE
*   tc off               - restore environment to pre-toolchain state

You can set TOOLCHAIN_DIRS (colon-separated) to where your toolchains live.
Defaults search to: "$HOME/x-tools:/opt:/usr/local:/opt/toolchains"

### Robust features:
 - Accepts version-suffixed executables (e.g. <triple>-gcc-12.2.0)
 - Works with GCC or Clang layouts
 - Can resolve by explicit path or by triple name
 - Saves/restores your previous env on "tc off"
 - Sets CC_FOR_BUILD/CXX_FOR_BUILD to native compilers (useful for host tools)

Example:
   . ./toolchain-env.sh
   
   export TOOLCHAIN_DIRS="$HOME/x-tools:/opt/ctng"
   
   tc list
   
   tc use aarch64-linux-gnu
   
   tc which
   
   make ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)"
   
   tc off



## Kernel Cleanup

Script to clean up old custom kernels and their modules by creating a
<kernel_ver>.tar.xz in a designated directory and then prompting for 
the removal of said kernel and modules.  I do all custom kernels so
for me this helps keep things a little more tidy.

  Usage: ./kernel_cleanup.sh <kernel-version>

  Example: ./kernel_cleanup.sh 6.16.5-jerryslab.patch-GIT12-v3.16+


## Swapmon

Fun little tool that will show what processes are actually swapped out.

Usage:

Default: simple table → PID SWAP(kB) CMD

* --full / -f: more columns → PID SWAP RSS VSZ CMD

* --json / -j: JSON snapshot

* --top / -t: continuously refreshing view (like top)

* --delay / -d SECS: refresh interval in --top mode (default 2s)

* --count / -n N: number of iterations in --top mode (default: infinite until Ctrl+C)

* --help / -h: usage

Swapmon only shows processes with VmSWAP > 0, i.e. actually in swap.


## Swapout  

### Utility to swap out a process.

* Force a process's memory to be pushed into swap by constraining it
   to a small-memory cgroup, then restoring the limit afterwards.
  
* Requires root (or sufficient privileges to manage cgroups and move PIDs).
 

  Usage:
   swapout PID [options]
 
  Options:

    -m, --limit-mb MB       Memory limit during swapout (default: 8 MB)

    -r, --target-rss-kb KB  Target RSS to reach before stopping (default: 16384 kB)

    -i, --interval SECS     Poll interval in seconds (default: 1.0)

    -n, --max-iter N        Maximum iterations before giving up (default: 60)

    -q, --quiet             Less verbose output

    -h, --help              Show this help


how it works...

* Puts a target PID into a temporary cgroup

* Applies a tight memory limit to force swapping

* Polls /proc/<pid>/status to watch VmRSS/VmSwap

* Restores the memory limit and cleans up

It supports both cgroup v1 (memory) and cgroup v2 (unified).


_ _Example_ _

  $sudo swapout 12345

Some tuning flags....

  $sudo swapout 12345 -m 8 -r 16384 -i 1 -n 60

   -m 8 → constrain to 8 MB during swapout

   -r 16384 → stop when RSS ≤ 16 MB

   -i 1 → poll every 1 second

   -n 60 → try up to 60 iterations

You should see output along the lines of....


   [+] swapout: targeting PID 12345

   [+] limit_mb=8, target_rss_kb=16384, interval=1.00, max_iter=60

   [+] cgroup v2 detected, using /sys/fs/cgroup/swapout/12345

   [+] Original limit at /sys/fs/cgroup/swapout/12345/memory.high: 'max'

   [+] Moved PID 12345 into /sys/fs/cgroup/swapout/12345

   [+] Applying temporary limit 8388608
    to /sys/fs/cgroup/swapout/12345/memory.high

   [+] Forcing swap... polling process memory usage
     iter  1: RSS=188000 kB, SWAP=0 kB
     iter  2: RSS=120000 kB, SWAP=64000 kB
     ...

   [+] Target RSS reached (<= 16384 kB), stopping.

   [+] Restoring limit at /sys/fs/cgroup/swapout/12345/memory.high to 'max'

   [+] Removed cgroup /sys/fs/cgroup/swapout/12345

   [+] swapout complete.









# Notes

To compile and install 

  make

  make install

To start over

  make clean

.. removes only the compiled C programs, not the scripts of course.
