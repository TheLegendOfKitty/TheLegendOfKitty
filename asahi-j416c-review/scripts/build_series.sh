#!/bin/sh
# Build the objects each patch touches, commit by commit, to check the
# series' "every commit compiles" claim (arm64, LLVM=1, Rust enabled).
#
# C files are built with -Werror (KCFLAGS); CONFIG_WERROR stays off because
# rustc >= 1.9x trips dead-code lints in the base tree's kernel crate.
#
# Usage: build_series.sh KERNEL_TREE BUILD_DIR BASE_REF [TIP_REF]
set -eu
TREE=$1 OUT=$2 BASE=$3 TIP=${4:-HEAD}
MK="make -C $TREE O=$OUT ARCH=arm64 LLVM=1 KCFLAGS=-Werror -j$(nproc)"

orig=$(git -C "$TREE" rev-parse --abbrev-ref HEAD)
trap 'git -C "$TREE" checkout -q "$orig"' EXIT

if [ ! -f "$OUT/.config" ]; then
	mkdir -p "$OUT"
	$MK defconfig
	"$TREE/scripts/config" --file "$OUT/.config" \
		-e RUST -e ARCH_APPLE -e APPLE_PMGR_MISC -e APPLE_PMP -e APPLE_PMP_REPORT \
		-e APPLE_PMGR_PWRSTATE -e ARM_APPLE_CPUIDLE -e CPU_IDLE -e DEBUG_FS \
		-e FTRACE -e ENABLE_DEFAULT_TRACERS -e NO_HZ_IDLE -e ENERGY_MODEL \
		-m DRM_APPLE -d WERROR
	$MK olddefconfig
fi

for c in $(git -C "$TREE" rev-list --reverse "$BASE..$TIP"); do
	git -C "$TREE" checkout -q "$c"
	files=$(git -C "$TREE" diff-tree --no-commit-id --name-only -r "$c")
	# *_template.c is #included by other units: build its whole directory
	objs=$(echo "$files" | grep -E '\.(c|rs)$' | grep -v '^include/' |
		sed -e 's|^\(.*/\)[^/]*_template\.c$|\1|' -e 's/\.\(c\|rs\)$/.o/' |
		sort -u | tr '\n' ' ')
	dts=$(echo "$files" | grep -q '^arch/arm64/boot/dts/apple/' && echo apple || true)
	subj=$(git -C "$TREE" log -1 --format=%s "$c")
	printf '=== %s %s\n' "$(git -C "$TREE" rev-parse --short "$c")" "$subj"
	if [ -n "$objs" ]; then
		if $MK $objs > "$OUT/last.log" 2>&1; then
			echo "    objects OK: $objs"
		else
			grep -E 'error|warning' "$OUT/last.log" | head -20
		fi
	fi
	if [ -n "$dts" ]; then
		$MK dtbs 2>&1 | grep -E 'error|warning' | grep -i apple || echo "    apple dtbs OK"
	fi
done
