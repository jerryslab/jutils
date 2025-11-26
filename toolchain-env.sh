#!/usr/bin/env bash
# toolchain-env.sh
# Source me:  . /path/to/toolchain-env.sh
#
# Author Jerry Richardson
# (C) Copyright 20205
#
# Provides a "tc" function to manage cross-compiler environments:
#   tc list              - list discovered toolchains (triples and roots)
#   tc use <triple|path> - activate toolchain by triple or by path to its root/bin/<triple>-gcc
#   tc which             - print current CC/CXX/CROSS_COMPILE
#   tc off               - restore environment to pre-toolchain state
#
# You can set TOOLCHAIN_DIRS (colon-separated) to where your toolchains live.
# Defaults search to: "$HOME/x-tools:/opt:/usr/local:/opt/toolchains"
#
# Robust features:
# - Accepts version-suffixed executables (e.g. <triple>-gcc-12.2.0)
# - Works with GCC or Clang layouts
# - Can resolve by explicit path or by triple name
# - Saves/restores your previous env on "tc off"
# - Sets CC_FOR_BUILD/CXX_FOR_BUILD to native compilers (useful for host tools)
#
# Example:
#   . ./toolchain-env.sh
#   export TOOLCHAIN_DIRS="$HOME/x-tools:/opt/ctng"
#   tc list
#   tc use aarch64-linux-gnu
#   tc which
#   make ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)"
#   tc off
#
# ---------------------------------------------------------------

: "${TOOLCHAIN_DIRS:=${HOME}/x-tools:/opt:/usr/local:/opt/toolchains}"

# ---------- internals ----------
_tc_old_env_saved=0
_tc_saved_vars=(
  CROSS_COMPILE CC CXX AR AS LD NM STRIP RANLIB OBJCOPY OBJDUMP
  CC_FOR_BUILD CXX_FOR_BUILD PKG_CONFIG_LIBDIR PKG_CONFIG_PATH
)

_tc_save_env() {
  (( _tc_old_env_saved )) && return 0
  for v in "${_tc_saved_vars[@]}"; do
    eval "_tc_backup_$v=\"\${$v-}\""
  done
  _tc_old_env_saved=1
}

_tc_restore_env() {
  (( _tc_old_env_saved )) || { echo "==> No toolchain env to restore."; return 0; }
  for v in "${_tc_saved_vars[@]}"; do
    eval "export $v=\"\${_tc_backup_$v-}\""
    unset "_tc_backup_$v"
  done
  _tc_old_env_saved=0
  echo "==> Toolchain env restored."
}

# Return first existing/executable file among args
_tc_first() {
  for f in "$@"; do
    [[ -n "$f" && -x "$f" && -f "$f" ]] && { printf '%s\n' "$f"; return 0; }
  done
  return 1
}

# Echo "<root>\t<triple>" on success
_tc_resolve() {
  local arg="$1" root= triple= bin=
  [[ -n "$arg" && "$arg" != "auto" ]] || return 1

  # Case 1: arg is an executable path (…/bin/<triple>-gcc* or clang*)
  if [[ -x "$arg" && -f "$arg" ]]; then
    bin="$(readlink -f "$arg")"
    root="$(dirname "$(dirname "$bin")")"
    local base="$(basename "$bin")"
    triple="${base%%-gcc*}"; [[ "$triple" != "$base" ]] || triple="${base%%-clang*}"
    [[ "$triple" != "$base" ]] || return 1
    printf '%s\t%s\n' "$root" "$triple"; return 0
  fi

  # Case 2: arg is a directory (root or root/bin)
  if [[ -d "$arg" ]]; then
    root="$(readlink -f "$arg")"
    if [[ -d "$root/bin" ]]; then
      for cand in "$root"/bin/*-gcc* "$root"/bin/*-clang*; do
        [[ -x "$cand" && -f "$cand" ]] || continue
        local base="$(basename "$cand")"
        triple="${base%%-gcc*}"; [[ "$triple" != "$base" ]] || triple="${base%%-clang*}"
        printf '%s\t%s\n' "$root" "$triple"; return 0
      done
    fi
    # Allow passing bin directory directly
    if [[ "$(basename "$root")" == "bin" ]]; then
      root="$(dirname "$root")"
      for cand in "$root"/bin/*-gcc* "$root"/bin/*-clang*; do
        [[ -x "$cand" && -f "$cand" ]] || continue
        local base="$(basename "$cand")"
        triple="${base%%-gcc*}"; [[ "$triple" != "$base" ]] || triple="${base%%-clang*}"
        printf '%s\t%s\n' "$root" "$triple"; return 0
      done
    fi
    return 1
  fi

  # Case 3: treat arg as a triple and search TOOLCHAIN_DIRS
  local gccbin clangbin d=
  IFS=: read -r -a dirs <<< "${TOOLCHAIN_DIRS}"
  for d in "${dirs[@]}"; do
    for root in "$d/$arg" "$d"; do
      [[ -d "$root/bin" ]] || continue
      # Accept version-suffixed names: <triple>-gcc*, <triple>-clang*
      for gccbin in "$root/bin/$arg-gcc"*; do
        [[ -x "$gccbin" && -f "$gccbin" ]] || continue
        printf '%s\t%s\n' "$root" "$arg"; return 0
      done
      for clangbin in "$root/bin/$arg-clang"*; do
        [[ -x "$clangbin" && -f "$clangbin" ]] || continue
        printf '%s\t%s\n' "$root" "$arg"; return 0
      done
    done
  done
  return 1
}

# Enumerate toolchains we can find
_tc_scan() {
  IFS=: read -r -a dirs <<< "$TOOLCHAIN_DIRS"
  {
    for d in "${dirs[@]}"; do
      [[ -d "$d" ]] || continue
      for root in "$d"/* "$d"; do
        [[ -d "$root/bin" ]] || continue
        # GCC
        for gccbin in "$root"/bin/*-gcc*; do
          [[ -x "$gccbin" && -f "$gccbin" ]] || continue
          triple="$(basename "${gccbin%%-gcc*}")"
          # Skip bare "gcc" without triple
          [[ "$triple" == "$(basename "$gccbin")" ]] && continue
          printf '%-30s  %s\n' "$triple" "$root"
        done
        # Clang
        for clangbin in "$root"/bin/*-clang*; do
          [[ -x "$clangbin" && -f "$clangbin" ]] || continue
          triple="$(basename "${clangbin%%-clang*}")"
          [[ "$triple" == "$(basename "$clangbin")" ]] && continue
          printf '%-30s  %s\n' "$triple" "$root"
        done
      done
    done
  } | sort -u
}

# Activate a toolchain by triple or path
_tc_use() {
  local spec="$1" line root triple bindir cc cxx
  if [[ -z "$spec" ]]; then
    echo "tc: usage: tc use <triple|path>"; return 2
  fi
  line="$(_tc_resolve "$spec")" || { echo "tc: could not find toolchain for '$spec'"; return 2; }
  root="${line%%$'\t'*}"
  triple="${line##*$'\t'}"
  bindir="$root/bin"

  _tc_save_env

  # Prefer gcc if present; accept version-suffixed executables
  cc="$(_tc_first \
      "$bindir/$triple-gcc" "$bindir/$triple-gcc-"* \
      "$bindir/$triple-clang" "$bindir/$triple-clang-"* )"
  if [[ -z "$cc" ]]; then
    echo "tc: no compiler found under $bindir for $triple"; return 3
  fi

  if [[ "$cc" == *clang* ]]; then
    cxx="$(_tc_first "${cc/clang/clang++}" "$bindir/$triple-clang++" "$bindir/$triple-clang++-"*)"
    [[ -n "$cxx" ]] || cxx="${cc/clang/clang++}"
  else
    cxx="$(_tc_first "$bindir/$triple-g++" "$bindir/$triple-g++-"* "$bindir/$triple-c++" "$bindir/$triple-c++-"*)"
    [[ -n "$cxx" ]] || cxx="${cc/gcc/g++}"
  fi

  export CROSS_COMPILE="$bindir/$triple-"
  export CC="$cc" CXX="$cxx"

  # Native compilers for host tools (GENIE, protobuf, etc.)
  export CC_FOR_BUILD="${CC_FOR_BUILD:-gcc}"
  export CXX_FOR_BUILD="${CXX_FOR_BUILD:-g++}"

  # Binutils (accept version-suffixed)
  export AR="$(_tc_first "$bindir/$triple-ar" "$bindir/$triple-ar-"*)"
  export AS="$(_tc_first "$bindir/$triple-as" "$bindir/$triple-as-"*)"
  export LD="$(_tc_first "$bindir/$triple-ld" "$bindir/$triple-ld-"*)"
  export NM="$(_tc_first "$bindir/$triple-nm" "$bindir/$triple-nm-"*)"
  export STRIP="$(_tc_first "$bindir/$triple-strip" "$bindir/$triple-strip-"*)"
  export RANLIB="$(_tc_first "$bindir/$triple-ranlib" "$bindir/$triple-ranlib-"*)"
  export OBJCOPY="$(_tc_first "$bindir/$triple-objcopy" "$bindir/$triple-objcopy-"*)"
  export OBJDUMP="$(_tc_first "$bindir/$triple-objdump" "$bindir/$triple-objdump-"*)"

  echo "==> Using toolchain: $triple"
  echo "    root: $root"
  echo "    CC:   $CC"
  echo "    CXX:  $CXX"
  echo "    CROSS_COMPILE: $CROSS_COMPILE"
}

tc() {
  case "$1" in
    list|"")
      echo "Searching in: ${TOOLCHAIN_DIRS}"
      _tc_scan
      ;;
    use)
      shift
      _tc_use "$1"
      ;;
    off|reset)
      _tc_restore_env
      ;;
    which)
      echo "CC=${CC-}"
      echo "CXX=${CXX-}"
      echo "CROSS_COMPILE=${CROSS_COMPILE-}"
      ;;
    help|-h|--help)
      cat <<'EOF'
tc list              - list discovered toolchains (triples and roots)
tc use <triple|path> - activate toolchain by triple or by path to its root/bin/<triple>-gcc
tc which             - print current CC/CXX/CROSS_COMPILE
tc off               - restore environment to pre-toolchain state

Environment:
  TOOLCHAIN_DIRS  Colon-separated directories to scan for toolchains.
                  Default: $HOME/x-tools:/opt:/usr/local:/opt/toolchains

Notes:
  * Accepts version-suffixed compilers (e.g. <triple>-gcc-12.2.0)
  * Works with GCC or Clang toolchains
  * Sets CC_FOR_BUILD/CXX_FOR_BUILD to native compilers for host-side tools
EOF
      ;;
    *)
      echo "tc: unknown command '$1' (try: tc list | tc use <triple|path> | tc off | tc which)"
      return 1
      ;;
  esac
}

# End of file
