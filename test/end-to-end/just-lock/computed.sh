#!/bin/sh
# Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

readonly JUST_LOCK="${PWD}/bin/lock-tool-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/build-output"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}"

# Set up repo foo
mkdir -p "${REPO_DIRS}/foo/src"
cd "${REPO_DIRS}/foo"
cat > repos.json <<'EOF'
{ "repositories":
  { "":
    { "repository":
      { "type": "file"
      , "path": "src"
      , "pragma": {"to_git": true}
      }
    }
  }
}
EOF
cat > src/TARGETS <<'EOF'
{ "": {"type": "export", "target": "gen"}
, "gen":
  {"type": "generic", "outs": ["foo.txt"], "cmds": ["echo -n FOO > foo.txt"]}
}
EOF
git init
git checkout --orphan foomaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add foo.txt' 2>&1

# Set up repo bar
mkdir -p "${REPO_DIRS}/bar"
cd "${REPO_DIRS}/bar"
cat > repos.in.json <<EOF
{ "repositories":
  { "": {"repository": "inner", "target_root": "computed_src"}
  , "inner":
    { "repository": {"type": "file", "path": ".", "pragma": {"to_git": true}}
    , "bindings": {"DoNotImport": "Missing"}
    }
  , "src":
    { "repository": {"type": "file", "path": "src", "pragma": {"to_git": true}}
    , "bindings": {"foo": "foo"}
    }
  , "computed_src":
    { "repository":
      { "type": "computed"
      , "repo": "src"
      , "target": ["", ""]
      , "config": {"COUNT": "10"}
      }
    }
  , "root_inner":
    { "repository": {"type": "file", "path": ".", "pragma": {"to_git": true}}
    , "bindings": {"AlsoDoNotImport": "AlsoMissing"}
    }
  , "root": {"repository": "root_inner", "bindings": {"foo": "foo"}}
  , "bar_root":
    { "repository":
      { "type": "computed"
      , "repo": "root"
      , "target": ["", ""]
      , "config": {"COUNT": "12"}
      }
    }
  }
, "imports":
  [ { "source": "git"
    , "repos": [{"alias": "foo"}]
    , "url": "${REPO_DIRS}/foo"
    , "branch": "foomaster"
    }
  ]
}
EOF

"${JUST_LOCK}" -C repos.in.json -o repos.json
cat repos.json

cat > generate.py <<'EOF'
import json
import sys

COUNT = int(sys.argv[1])
targets = {}
for i in range(COUNT):
  targets["%d" % i] = {"type": "generic", "outs": ["%d.txt" %i],
                       "cmds": ["seq 0 %d > %d.txt" % (i, i)]}
targets[""] = {"type": "export", "target": "gen"}
targets["gen"] = {"type": "generic",
               "deps": ["%d" % i for i in range(COUNT)],
               "cmds": [" ".join(["cat"] + ["%d.txt" % i for i in range(COUNT)]
                                 + ["> out"])],
               "outs": ["out"]}
print (json.dumps(targets, indent=2))
EOF
cat > TARGETS <<'EOF'
{ "": {"type": "export", "flexible_config": ["COUNT"], "target": "generate"}
, "generate":
  { "type": "generic"
  , "arguments_config": ["COUNT"]
  , "outs": ["TARGETS", "bar.txt"]
  , "deps": ["generate.py", ["@", "foo", "", ""]]
  , "cmds":
    [ { "type": "join"
      , "separator": " "
      , "$1":
        [ "python3"
        , "generate.py"
        , {"type": "var", "name": "COUNT"}
        , ">"
        , "TARGETS"
        ]
      }
    , "cat foo.txt | tr A-Z a-z > bar.txt"
    ]
  }
}
EOF
mkdir src
cp generate.py src/generate.py
cp TARGETS src/TARGETS

git init
git checkout --orphan barmaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add foo.txt' 2>&1

# Set up repo to build
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > TARGETS <<'EOF'
{ "": {"type": "export", "target": "gen"}
, "gen":
  { "type": "generic"
  , "cmds": ["cat bar.txt init.txt > out.txt"]
  , "outs": ["out.txt"]
  , "deps": [["@", "bar_root", "", ""], "init"]
  }
, "init":
  { "type": "generic"
  , "cmds": ["cat foo.txt bar.txt > init.txt"]
  , "outs": ["init.txt"]
  , "deps": [["@", "foo", "", ""], ["@", "bar", "", ""]]
  }
}
EOF
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": ".", "pragma": {"to_git": true}}
    , "bindings": {"foo": "foo", "bar": "bar", "bar_root": "bar_root"}
    }
  }
, "imports":
  [ { "source": "git"
    , "repos": [{"alias": "foo"}]
    , "url": "${REPO_DIRS}/foo"
    , "branch": "foomaster"
    }
  , { "source": "git"
    , "repos": [{"alias": "bar"}]
    , "url": "${REPO_DIRS}/bar"
    , "branch": "barmaster"
    }
  , { "source": "git"
    , "repos": [{"repo": "bar_root"}]
    , "url": "${REPO_DIRS}/bar"
    , "branch": "barmaster"
    }
  ]
}
EOF
"${JUST_LOCK}" -C repos.in.json -o repos.json

echo
cat repos.json
grep DoNotImport && exit 1 || :  # we should not bring in unneeded bindings
echo
"${JUST_MR}" -C repos.json --norc --just "${JUST}" \
             --local-build-root "${LBR}" analyse \
             -L '["env", "PATH='"${PATH}"'"]' 2>&1

echo "OK"