 # JUTILS


 
Various command line utilities to make sysad's life easier.  The commands
are as follows 

#######################################################################

KERNMEM:

Computes a practical, real-world estimate of total kernel memory usage using
/proc
It gathers:

 Kernel code
 Kernel data
 Kernel bss
 Slab (total, reclaimable, unreclaimable)
 Page tables
 Vmalloc allocations
 Total module memory (lsmod equivalent parsing /proc/modules)

And prints subtotals + grand total and workd on any linux kernel that supports
/proc/meminfo and /proc/modules.

Esitmate also includes... 

real kernel image memory (text, data, bss), slab allocations, page tables, 
module memory and vmalloc area usage.

The program will compensate for kernels that do not expose the fields KernelCode,
KernelData and KernelBss.  In this case it will try to read from /proc/kallsyms or
System.map in case kernel address space layout randomization cock-blocks ya. ;-)


#######################################################################

CLIPIT - OSC-52 clipboard copier.

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
