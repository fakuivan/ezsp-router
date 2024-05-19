#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT="$SCRIPT_DIR/.."
GSDK_IMAGE="$1"
PATCHES_DIR="$2"
DOCKER_CMD="$3"
shift 3
DOCKER_ARGS=("$@")
GSDK_PROJECT_PATH=protocol/zigbee/app/framework/scenarios/z3/ZigbeeMinimalHost/ZigbeeMinimalHost.slcp
GSDK_PROJECT_FILE="$(basename "$GSDK_PROJECT_PATH")"

src_clean () {
  [[ -z "$(git -C "$PROJECT_ROOT" ls-files --exclude-standard --others src/)" ]] 
}

echoerr () {
  echo "$@" 1>&2
}

bash_in_gsdk () {
  "$DOCKER_CMD" "${DOCKER_ARGS[@]}" run \
    -v "$SCRIPT_DIR"/../src/:/src/ \
    --rm \
    "$GSDK_IMAGE" \
    bash -c "$1"
}

gen_sdk () {
  bash_in_gsdk "
    STUDIO_ADAPTER_PACK_PATH=/tools/zap/ \
      /tools/slc-cli/slc generate \
      --tt \
      -np \
      --sdk /tools/gsdk/ \
      /tools/gsdk/${GSDK_PROJECT_PATH@Q} \
      -cp \
      -d /src/ &&
      chown $(id -u):$(id -g) -R /src/"
}

regen_sdk () {
  bash_in_gsdk "
    STUDIO_ADAPTER_PACK_PATH=/tools/zap/ \
      /tools/slc-cli/slc generate \
      --tt \
      --sdk /tools/gsdk/ \
      /src/${GSDK_PROJECT_FILE@Q} \
      -cp \
      -d /src/ &&
      chown $(id -u):$(id -g) -R /src/"
}

apply_patches () {
  "$PROJECT_ROOT"/apply_patches.sh "$PATCHES_DIR"
}

if [[ ! -f "$PROJECT_ROOT"/src/"$GSDK_PROJECT_FILE" ]]; then
  if ! src_clean; then
    echoerr The /src directory contains untracked or uncommited changes
    echoerr Please stash or commit them before generating project
    exit 1
  fi
  RESTORE_SRC=
  gen_sdk

  echo Applying patches
  apply_patches
else
  echo Skipping initial project generation from GSDK since project file already exists
fi

echo Regenerating project
regen_sdk

echo Reapplying patches "(might not be required)"
apply_patches

if [[ -n ${RESTORE_SRC+x} ]]; then
  echo Restoring src/
  git -C "$PROJECT_ROOT" restore src/
fi
