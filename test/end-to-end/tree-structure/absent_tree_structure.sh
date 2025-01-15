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

set -e

readonly ROOT="$(pwd)"
readonly LBRDIR="$TMPDIR/local-build-root"
readonly RCFILE="${TEST_TMPDIR}/mrrc.json"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly JUST_MR="${ROOT}/bin/mr-tool-under-test"
readonly OUT="${ROOT}/out"
mkdir -p "${OUT}"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

readonly MAIN_REPO="${ROOT}/main"
mkdir -p "${MAIN_REPO}"
cd "${MAIN_REPO}"
touch ROOT

cat > "TARGETS" << 'EOF'
{ "": {"type": "export", "target": "generate"}
, "generate":
  { "type": "generic"
  , "deps": [["TREE", null, "src"]]
  , "outs": ["result.txt"]
  , "cmds": ["ls -R src > result.txt"]
  }
}
EOF

echo
echo TARGETS:
cat TARGETS

cat > repo-config.json <<EOF
{ "repositories":
  { "targets":
    { "repository":
      { "type": "file"
      , "path": "."
      , "pragma": {"to_git": true}
      }
    }
  , "foo":
    { "repository":
      { "type": "git tree"
      , "id": "${TREE_0}"
      , "cmd": []
      }
    }
  , "structure_foo":
    { "repository":
      { "type": "tree structure"
      , "repo": "foo"
      }
    }
  , "result_foo":
    { "repository": "structure_foo"
    , "target_root": "targets"
    }
  , "bar":
    { "repository":
      { "type": "git tree"
      , "id": "${TREE_1}"
      , "cmd": []
      }
    }
  , "structure_bar":
    { "repository":
      { "type": "tree structure"
      , "repo": "bar"
      }
    }
  , "result_bar":
    { "repository": "structure_bar"
    , "target_root": "targets"
    }
  }
}
EOF

echo
echo repo-config.json:
cat repo-config.json

cat > "${RCFILE}" <<'EOF'
{"absent": [{"root": "workspace", "path": "absent.json"}]}
EOF

echo
echo mrrc.json:
cat "${RCFILE}"

# Test local tree structure of absent roots:
# Set both source repositories foo and bar absent:
cat > absent.json <<'EOF'
["foo", "bar"]
EOF

echo
echo "Local tree structure root of an absent root."
echo "Expected to be computed on serve and downloaded:"
("${JUST_MR}" --rc "${RCFILE}" \
    --local-build-root "${LBRDIR}/local" -C repo-config.json \
    -r "${REMOTE_EXECUTION_ADDRESS}" -R "${SERVE}" ${COMPAT} \
    --main result_foo -L '["env", "PATH='"${PATH}"'"]' --log-limit 4 \
    --just "${JUST}" install -o "${OUT}/result_foo" 2>&1) \
    > "${OUT}/log_result_foo"

echo
cat "${OUT}/log_result_foo"

grep 'Root \["tree structure", "foo"\] has been downloaded from the remote end point' \
      "${OUT}/log_result_foo"

echo
cat "${OUT}/result_foo/result.txt"

echo
echo "Local tree structure root of an absent root."
echo "Expected to be taken from local cache:"
("${JUST_MR}" --rc "${RCFILE}" \
    --local-build-root "${LBRDIR}/local" -C repo-config.json \
    -r "${REMOTE_EXECUTION_ADDRESS}" -R "${SERVE}" ${COMPAT} \
    --main result_bar -L '["env", "PATH='"${PATH}"'"]' --log-limit 4 \
    --just "${JUST}" install -o "${OUT}/result_bar" 2>&1) \
    > "${OUT}/log_result_bar"

echo
cat "${OUT}/log_result_bar"

# The repo "bar" is a structural copy of "foo" with  a different content of
# the file, so tree_structure(foo) == tree_structure(bar).
# Given the fact that tree_structure(foo) has been already evaluated
# and downloaded from the remote end point, there is no need to download it
# one more time. tree_structure(bar) gets taken from the local cache.
grep 'Root \["tree structure", "bar"\] has been taken from local cache' \
      "${OUT}/log_result_bar"

echo
cat "${OUT}/result_bar/result.txt"

echo OK
