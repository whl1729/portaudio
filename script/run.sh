SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P)"
PROJECT_DIR="${SCRIPT_DIR}/.."
LIB_DIR="${PROJECT_DIR}/lib/.libs"

export LD_LIBRARY_PATH=${LIB_DIR}:${LD_LIBRARY_PATH}
"$@"
