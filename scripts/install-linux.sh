#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mcc_path=""
dll_path=""
uninstall=false

usage() {
    cat <<'EOF'
Usage: install-linux.sh [--mcc PATH] [--dll PATH] [--uninstall]

  --mcc PATH    MCC game directory or MCC/Binaries/Win64 directory
  --dll PATH    WTSAPI32.dll to install
  --uninstall   Restore the previous DLL, or remove AlphaRing's DLL
EOF
}

while (($#)); do
    case "$1" in
        --mcc)
            mcc_path="${2:-}"
            shift 2
            ;;
        --dll)
            dll_path="${2:-}"
            shift 2
            ;;
        --uninstall)
            uninstall=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown argument: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

find_win64() {
    local base="${1%/}"
    local candidate
    for candidate in \
        "$base" \
        "$base/MCC/Binaries/Win64" \
        "$base/mcc/binaries/win64"; do
        if [[ -f "$candidate/MCC-Win64-Shipping.exe" ]]; then
            printf '%s\n' "$(cd -- "$candidate" && pwd)"
            return 0
        fi
    done
    return 1
}

declare -a steam_roots=()
add_steam_root() {
    local root="${1%/}"
    [[ -d "$root/steamapps" ]] || return 0
    local existing
    for existing in "${steam_roots[@]:-}"; do
        [[ "$existing" == "$root" ]] && return 0
    done
    steam_roots+=("$root")
}

add_steam_root "$HOME/.steam/steam"
add_steam_root "$HOME/.steam/root"
add_steam_root "$HOME/.local/share/Steam"
add_steam_root "$HOME/.var/app/com.valvesoftware.Steam/data/Steam"

shopt -s nullglob
for root in /mnt/*/SteamLibrary /media/"$USER"/*/SteamLibrary /run/media/"$USER"/*/SteamLibrary; do
    add_steam_root "$root"
done
shopt -u nullglob

for root in "${steam_roots[@]:-}"; do
    vdf="$root/steamapps/libraryfolders.vdf"
    [[ -f "$vdf" ]] || continue
    while IFS= read -r line; do
        if [[ "$line" =~ \"path\"[[:space:]]+\"([^\"]+)\" ]]; then
            library_path="${BASH_REMATCH[1]//\\\\/\\}"
            add_steam_root "$library_path"
        fi
    done < "$vdf"
done

win64=""
if [[ -n "$mcc_path" ]]; then
    if ! win64="$(find_win64 "$mcc_path")"; then
        printf 'Could not find MCC/Binaries/Win64 below: %s\n' "$mcc_path" >&2
        exit 1
    fi
else
    declare -a installs=()
    for root in "${steam_roots[@]:-}"; do
        candidate="$root/steamapps/common/Halo The Master Chief Collection/MCC/Binaries/Win64"
        [[ -f "$candidate/MCC-Win64-Shipping.exe" ]] || continue
        duplicate=false
        for existing in "${installs[@]:-}"; do
            [[ "$existing" == "$candidate" ]] && duplicate=true
        done
        $duplicate || installs+=("$candidate")
    done

    if ((${#installs[@]} == 1)); then
        win64="${installs[0]}"
    elif ((${#installs[@]} > 1)); then
        printf 'Multiple MCC installations found:\n'
        for i in "${!installs[@]}"; do
            printf '  %d) %s\n' "$((i + 1))" "${installs[i]}"
        done
        read -r -p "Choose installation [1-${#installs[@]}]: " selection
        [[ "$selection" =~ ^[0-9]+$ ]] && ((selection >= 1 && selection <= ${#installs[@]})) || {
            printf 'Invalid selection.\n' >&2
            exit 1
        }
        win64="${installs[selection - 1]}"
    elif [[ -t 0 ]]; then
        read -r -p 'MCC game directory: ' mcc_path
        if ! win64="$(find_win64 "$mcc_path")"; then
            printf 'Could not find MCC/Binaries/Win64 below that directory.\n' >&2
            exit 1
        fi
    else
        printf 'MCC was not detected. Pass --mcc "/path/to/Halo The Master Chief Collection".\n' >&2
        exit 1
    fi
fi

target="$win64/WTSAPI32.dll"
backup="$target.alpharing-backup"

if $uninstall; then
    if [[ -f "$backup" ]]; then
        mv -f -- "$backup" "$target"
        printf 'Restored previous DLL: %s\n' "$target"
    elif [[ -f "$target" ]]; then
        rm -f -- "$target"
        printf 'Removed: %s\n' "$target"
    else
        printf 'AlphaRing DLL is not installed at: %s\n' "$target"
    fi
    exit 0
fi

if [[ -z "$dll_path" ]]; then
    for candidate in \
        "$script_dir/../WTSAPI32.dll" \
        "$script_dir/WTSAPI32.dll" \
        "$script_dir/../build-mingw/WTSAPI32.dll"; do
        if [[ -f "$candidate" ]]; then
            dll_path="$candidate"
            break
        fi
    done
fi

if [[ ! -f "$dll_path" ]]; then
    printf 'WTSAPI32.dll was not found. Pass --dll /path/to/WTSAPI32.dll.\n' >&2
    exit 1
fi

if [[ -f "$target" && ! -f "$backup" ]]; then
    cp -a -- "$target" "$backup"
    printf 'Backed up existing DLL: %s\n' "$backup"
fi

install -m 0755 -- "$dll_path" "$target"

game_root="$(dirname -- "$(dirname -- "$(dirname -- "$win64")")")"
resource_source=""
for candidate in "$script_dir/../res" "$script_dir/../alpha_ring" "$script_dir/res" "$script_dir/alpha_ring"; do
    if [[ -d "$candidate" ]]; then
        resource_source="$candidate"
        break
    fi
done
if [[ -n "$resource_source" ]]; then
    mkdir -p -- "$game_root/alpha_ring"
    cp -a -- "$resource_source/." "$game_root/alpha_ring/"
fi

printf '\nInstalled AlphaRing to:\n  %s\n' "$target"
printf '\nSteam launch options:\n  WINEDLLOVERRIDES="WTSAPI32=n,b" %%command%%\n'
printf '\nLaunch MCC with anti-cheat disabled.\n'
