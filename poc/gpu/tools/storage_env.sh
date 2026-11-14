# Optional, local-development helper. Source it:  . poc/gpu/tools/storage_env.sh
# Keeps large files off the machine's internal disk: everything big goes under ./storage at the repository root, which lives on the external volume and is
# ignored by git (/storage/ in the root .gitignore). Sets STORAGE and BAZELISK_HOME, creates the subfolders, and defines `bz`, a bazelisk wrapper that keeps
# Bazel's install, output base and repository cache under $STORAGE/bazel instead of ~/Library/Caches. Nothing in CI uses it.
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || (cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../../.." && pwd))"
export STORAGE="${STORAGE:-$ROOT/storage}"
mkdir -p "$STORAGE"/bazel "$STORAGE"/bazelisk "$STORAGE"/downloads "$STORAGE"/mesh "$STORAGE"/tmp
export BAZELISK_HOME="$STORAGE/bazelisk"
# Warn if the storage folder is on the same device as /, i.e. the internal disk.
if [ "$(stat -f %d "$STORAGE" 2>/dev/null || stat -c %d "$STORAGE")" = "$(stat -f %d / 2>/dev/null || stat -c %d /)" ]; then
  echo "storage_env: WARNING $STORAGE is on the same disk as / (not an external volume)" >&2
fi
bz() { "${BAZELISK:-bazelisk}" --output_user_root="$STORAGE/bazel" "$@"; }
