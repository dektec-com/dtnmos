#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#*#*# nmos_testbed.sh *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# dtnmos - Installs, starts and stops an NMOS test bed: a registry and the Testing Tool
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Installs, starts and stops an NMOS test bed: the NMOS registry (nmos-cpp with the
# nmos-js controller) and the AMWA NMOS Testing Tool, as rootless podman quadlets of
# this user.
#
#   nmos-testbed install [--address IP] [--priority N]
#                       set up both on this machine, start them, and put this script
#                       in ~/.local/bin
#   nmos-testbed uninstall
#                       stop both and remove their units; keeps ~/nmos and the images
#   nmos-testbed start|stop|restart|status [registry|testing|all]
#   nmos-testbed logs registry|testing      follow the log, Ctrl+C to leave
#   nmos-testbed update [registry|testing|all]   pull the latest image and restart
#   nmos-testbed autostart on|off [registry|testing|all]   start at boot or not
#
# Without a service name, a command applies to both. The registry's settings are in
# ~/nmos/registry.json; restart it after changing them. Needs podman 4.4 or later.

set -euo pipefail

NMOS_DIR="$HOME/nmos"
UNITS_DIR="$HOME/.config/containers/systemd"
PARKED_DIR="$NMOS_DIR/parked"
REGISTRY_IMAGE=docker.io/rhastie/nmos-cpp:latest
TESTING_IMAGE=docker.io/amwa/nmos-testing:latest

usage()
{
    sed -n '8,23p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
}

die()
{
    echo "nmos-testbed: $*" >&2
    exit 1
}

services()
{
    case "${1:-all}" in
        registry) echo nmos-registry ;;
        testing)  echo nmos-testing ;;
        all)      echo nmos-registry nmos-testing ;;
        *)        usage ;;
    esac
}

image_of()
{
    case "$1" in
        nmos-registry) echo "$REGISTRY_IMAGE" ;;
        nmos-testing)  echo "$TESTING_IMAGE" ;;
    esac
}

registry_address()
{
    sed -n 's/.*"host_address": *"\([^"]*\)".*/\1/p' "$NMOS_DIR/registry.json" 2>/dev/null
}

show_urls()
{
    local host
    host="$(registry_address)"
    [ -n "$host" ] || host="$(hostname -f)"
    echo "Registry:      http://$host:8010/x-nmos"
    echo "Controller:    http://$host:8010/admin"
    echo "Testing Tool:  http://$host:5000"
}

show_states()
{
    local s
    for s in "$@"; do
        printf '%-14s %s\n' "$s:" "$(systemctl --user is-active "$s" 2>/dev/null || true)"
    done
}

check_installed()
{
    [ -f "$UNITS_DIR/nmos-registry.container" ] ||
        [ -f "$PARKED_DIR/nmos-registry.container" ] ||
        die "not installed here; run 'nmos-testbed install' first"
}

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Install and uninstall +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

write_registry_json()
{
    local address="$1" priority="$2"
    cat > "$NMOS_DIR/registry.json" <<EOF
{
"pri": $priority,
"logging_level": 0,
"http_trace": false,
"label": "nmos-registry on $(hostname -s)",
"host_address": "$address",
"host_addresses": ["$address"],
"http_port": 8010,
"query_ws_port": 8011,
"registration_expiry_interval": 12
}
EOF
}

write_unit()
{
    local name="$1" description="$2" image="$3" volume="$4"
    cat > "$UNITS_DIR/$name.container" <<EOF
[Unit]
Description=$description
After=network-online.target
Wants=network-online.target

[Container]
Image=$image
ContainerName=$name
Network=host
# An init process forwards SIGTERM; the image's own entry script ignores it.
RunInit=true
$volume
[Service]
Restart=always
# A container stopped by SIGTERM exits with 143, or 137 if it had to be killed; both
# are a stop, not a failure.
SuccessExitStatus=143 137

[Install]
WantedBy=default.target
EOF
}

# The machine's one IPv4 address that is not on a DekTec card. A DekTec network port
# (MAC prefix 00:14:f4) can hold the default route on a test machine, and the registry
# belongs on the machine's own interface. With more than one candidate, --address decides.
choose_address()
{
    local candidates=() itf addr
    while read -r itf addr; do
        case "$(cat "/sys/class/net/$itf/address" 2>/dev/null)" in
            00:14:f4:*) continue ;;
        esac
        candidates+=("$itf ${addr%/*}")
    done < <(ip -4 -o addr show scope global | awk '{print $2, $4}')

    if [ ${#candidates[@]} -ne 1 ]; then
        echo "nmos-testbed: cannot choose the address; give --address with one of:" >&2
        ip -4 -o addr show scope global | awk '{print "  " $4 " (" $2 ")"}' >&2
        exit 1
    fi
    echo "${candidates[0]#* }"
}

install()
{
    local address="" priority=10
    while [ $# -gt 0 ]; do
        case "$1" in
            --address)  address="${2:-}"; shift 2 ;;
            --priority) priority="${2:-}"; shift 2 ;;
            *)          usage ;;
        esac
    done

    command -v podman >/dev/null ||
        die "podman is not installed; install it with: sudo apt install podman"
    local version
    version="$(podman version --format '{{.Client.Version}}')"
    [ "$(printf '%s\n4.4\n' "$version" | sort -V | head -1)" = 4.4 ] ||
        die "podman $version has no quadlets; 4.4 or later is needed"

    [ -n "$address" ] || address="$(choose_address)"
    echo "Address: $address, priority $priority"

    mkdir -p "$NMOS_DIR" "$UNITS_DIR" "$PARKED_DIR"
    if [ -f "$NMOS_DIR/registry.json" ]; then
        echo "Keeping the existing $NMOS_DIR/registry.json"
    else
        write_registry_json "$address" "$priority"
    fi
    rm -f "$PARKED_DIR"/nmos-*.container
    write_unit nmos-registry "NMOS registry and controller (nmos-cpp, nmos-js)" \
        "$REGISTRY_IMAGE" "Volume=%h/nmos/registry.json:/home/registry.json:ro,Z
"
    write_unit nmos-testing "AMWA NMOS Testing Tool" "$TESTING_IMAGE" ""

    # Without lingering, the services stop at logout and do not start at boot.
    if [ "$(loginctl show-user "$USER" -p Linger --value 2>/dev/null)" != yes ]; then
        loginctl enable-linger "$USER" 2>/dev/null ||
            echo "Could not enable lingering; run: sudo loginctl enable-linger $USER"
    fi

    local self target="$HOME/.local/bin/nmos-testbed"
    self="$(readlink -f "$0")"
    if [ "$self" != "$(readlink -f "$target" 2>/dev/null)" ]; then
        mkdir -p "$(dirname "$target")"
        cp "$self" "$target"
        chmod +x "$target"
        echo "Installed $target"
    fi

    podman pull "$REGISTRY_IMAGE"
    podman pull "$TESTING_IMAGE"
    systemctl --user daemon-reload
    systemctl --user restart nmos-registry nmos-testing
    echo "Installed. The Testing Tool takes some seconds before it answers."
    show_urls
}

uninstall()
{
    systemctl --user stop nmos-registry nmos-testing 2>/dev/null || true
    rm -f "$UNITS_DIR"/nmos-registry.container "$UNITS_DIR"/nmos-testing.container \
        "$PARKED_DIR"/nmos-*.container
    systemctl --user daemon-reload
    echo "Removed the services. $NMOS_DIR and the images remain;"
    echo "remove them with: rm -r $NMOS_DIR; podman rmi $REGISTRY_IMAGE $TESTING_IMAGE"
}

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Autostart +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

autostart()
{
    local mode="$1"
    shift
    mkdir -p "$PARKED_DIR"
    for s in "$@"; do
        if [ "$mode" = on ] && [ -f "$PARKED_DIR/$s.container" ]; then
            mv "$PARKED_DIR/$s.container" "$UNITS_DIR/"
        elif [ "$mode" = off ] && [ -f "$UNITS_DIR/$s.container" ]; then
            systemctl --user stop "$s" || true
            mv "$UNITS_DIR/$s.container" "$PARKED_DIR/"
        fi
    done
    systemctl --user daemon-reload
    for s in "$@"; do
        if [ -f "$UNITS_DIR/$s.container" ]; then
            echo "$s: starts at boot"
        else
            echo "$s: does not start at boot, nor by hand until 'autostart on'"
        fi
    done
}

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

command="${1:-}"
[ -n "$command" ] || usage
shift

case "$command" in
    install)
        install "$@"
        ;;
    uninstall)
        uninstall
        ;;
    start|stop|restart)
        check_installed
        read -r -a units <<< "$(services "${1:-all}")"
        systemctl --user "$command" "${units[@]}"
        show_states "${units[@]}"
        [ "$command" = stop ] || show_urls
        ;;
    status)
        check_installed
        read -r -a units <<< "$(services "${1:-all}")"
        show_states "${units[@]}"
        show_urls
        ;;
    logs)
        [ $# -eq 1 ] && [ "$1" != all ] || usage
        podman logs -f "$(services "$1")"
        ;;
    update)
        check_installed
        read -r -a units <<< "$(services "${1:-all}")"
        for s in "${units[@]}"; do
            podman pull "$(image_of "$s")"
        done
        systemctl --user restart "${units[@]}"
        ;;
    autostart)
        check_installed
        mode="${1:-}"
        [ "$mode" = on ] || [ "$mode" = off ] || usage
        read -r -a units <<< "$(services "${2:-all}")"
        autostart "$mode" "${units[@]}"
        ;;
    *)
        usage
        ;;
esac
