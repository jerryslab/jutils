# **JUTILS**

 
_Various command line utilities to make a sysad's life easier._  

#######################################################################

##KERNMEM:

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


#######################################################################

##CLIPIT - OSC-52 clipboard copier.

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

#######################################################################

##toolchain-env.sh - cross-compile toolchain set-up.

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


#######################################################################

##Kernel Cleanup

Script to clean up old custom kernels and their modules by creating a
<kernel_ver>.tar.xz in a designated directory and then prompting for 
the removal of said kernel and modules.  I do all custom kernels so
for me this helps keep things a little more tidy.

  Usage: ./kernel_cleanup.sh <kernel-version>

  Example: ./kernel_cleanup.sh 6.16.5-jerryslab.patch-GIT12-v3.16+

#######################################################################

