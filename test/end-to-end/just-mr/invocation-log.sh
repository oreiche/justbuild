#!/bin/sh
# Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


set -eu

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly LOG_DIR="${PWD}/log"
readonly ETC_DIR="${PWD}/etc"
readonly WRK_DIR="${PWD}/work"
readonly REPORTED_CONFIG="${PWD}/out/just-repo-config.json"
readonly OUT_GRAPH="${TEST_TMPDIR}/cmdline-option-graph.json"

# Set up an rc file, requesting invocation logging
mkdir -p "${ETC_DIR}"
readonly RC="${ETC_DIR}/rc.json"
cat > "${RC}" <<EOF
{ "invocation log":
  { "directory": {"root": "system", "path": "${LOG_DIR#/}"}
  , "metadata": "meta.json"
  , "--dump-graph": "graph.json"
  , "--profile": "profile.json"
  }
, "rc files": [{"root": "workspace", "path": "rc.json"}]
, "just": {"root": "system", "path": "${JUST#/}"}
, "local build root": {"root": "system", "path": "${LBR#/}"}
}
EOF
cat "${RC}"

# Setup basic project, setting project id
mkdir -p "${WRK_DIR}"
cd "${WRK_DIR}"
touch ROOT
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}
EOF

cat > rc.json <<'EOF'
{"invocation log": {"project id": "invocation-log-test"}}
EOF

cat > TARGETS <<'EOF'
{ "upper":
  { "type": "generic"
  , "deps": ["data.txt"]
  , "outs": ["upper.txt"]
  , "cmds": ["cat data.txt | tr a-z A-Z > upper.txt"]
  }
}
EOF
echo blablabla > data.txt

# Call analyse via just-mr
"${JUST_MR}" --rc "${RC}" analyse --dump-graph "${OUT_GRAPH}" upper 2>&1

# As this is the first invocation, we can find the invocation-log dir by a glob
INVOCATION_DIR="$(ls -d "${LOG_DIR}"/invocation-log-test/*)"

# ... this should create a metadata file.
METADATA_FILE="${INVOCATION_DIR}/meta.json"
echo "Meta data file ${METADATA_FILE}"
cat "${METADATA_FILE}"
echo

# Sanity check: the local build root must occur in the command line as
# it was specified in the rc file.
[ $(jq '.cmdline | contains(["'"${LBR}"'"])' "${METADATA_FILE}") = true ]

# Install the referenced configuration

"${JUST_MR}" --rc "${RC}" install-cas -o "${REPORTED_CONFIG}" \
  $(jq -r '.configuration' "${METADATA_FILE}") 2>&1

echo
cat "${REPORTED_CONFIG}"
echo

# ... and verify that the file repository contains the correct path
[ "$(jq -r '.repositories.""."workspace_root"[1]' "${REPORTED_CONFIG}")" = "${WRK_DIR}" ]

# Verify the presence of graph file
GRAPH_FILE="${INVOCATION_DIR}/graph.json"
[ -f "${GRAPH_FILE}" ]
echo "Graph file ${GRAPH_FILE}"
cat "${GRAPH_FILE}"

# ... and do some basic sanity check
[ $(jq '.actions | keys | length' "${GRAPH_FILE}") -eq 1 ]
ACTION_KEY="$(jq '.actions | keys | .[0]' "${GRAPH_FILE}")"
[ "$(jq -r '.actions.'"${ACTION_KEY}"'.output[0]' "${GRAPH_FILE}")" = "upper.txt" ]

# Also verify that the passed graph option is honored as well
[ -f "${OUT_GRAPH}" ]
cmp "${GRAPH_FILE}" "${OUT_GRAPH}"

# Verify the profile file
PROFILE_FILE="${INVOCATION_DIR}/profile.json"
cat "${PROFILE_FILE}"
[ $(jq '."exit code"' "${PROFILE_FILE}") -eq 0 ]
[ $(jq -r '."target"[3]' "${PROFILE_FILE}") = "upper" ]

echo OK
