#!/usr/bin/env python3
# Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

import json
import sys

from typing import Any, List, Optional

# generic JSON type
Json = Any

def log(*args: str, **kwargs: Any) -> None:
    print(*args, file=sys.stderr, **kwargs)

def fail(s: str, exit_code: int = 1):
    log(f"Error: {s}")
    sys.exit(exit_code)


def roots_equal(a: Json, b: Json) -> bool:
    if a["type"] != b["type"]:
        return False
    if a["type"] == "file":
        return a["path"] == b["path"]
    elif a["type"] in ["archive", "zip"]:
        return (a["content"] == b["content"]
                and a.get("subdir", ".") == b.get("subdir", "."))
    elif a["type"] == "git":
        return (a["commit"] == b["commit"]
                and a.get("subdir", ".") == b.get("subdir", "."))
    else:
        # unknown repository type, the only safe way is to test
        # for full equality
        return a == b

def get_root(repos: Json, name: str, *, root_name: str="repository",
             default_root : Optional[Json]=None) -> Json:
    root = repos[name].get(root_name)
    if root is None:
        if default_root is not None:
            return default_root
        else:
            fail("Did not find mandatory root %s" % (name,))
    if isinstance(root, str):
        return get_root(repos, root)
    return root

def local_repos_equal(repos: Json, name_a: str, name_b: str) -> bool:
    if name_a == name_b:
        return True
    root_a = None
    root_b = None
    for root_name in ["repository",
                      "target_root", "rule_root", "expression_root"]:
        root_a = get_root(repos, name_a, root_name=root_name,
                          default_root = root_a)
        root_b = get_root(repos, name_b, root_name=root_name,
                          default_root = root_b)
        if not roots_equal(root_a, root_b):
            return False
    for file_name, default_name in [("target_file_name", "TARGETS"),
                                    ("rule_file_name", "RULES"),
                                    ("expression_file_name", "EXPRESSIONS")]:
        fname_a = repos[name_a].get(file_name, default_name)
        fname_b = repos[name_b].get(file_name, default_name)
        if fname_a != fname_b:
            return False
    open_names_a = set(repos[name_a].get("bindings", {}).keys())
    open_names_b = set(repos[name_b].get("bindings", {}).keys())
    if open_names_a != open_names_b:
        return False
    return True

def bisimilar_repos(repos: Json) -> List[List[str]]:
    """Compute the maximal bisimulation between the repositories
    and return the bisimilarity classes."""
    bisim = {}

    def is_different(name_a: str, name_b: str) -> bool:
        return bisim.get((name_a, name_b), {}).get("different", False)

    def mark_as_different(name_a: str, name_b: str):
        nonlocal bisim
        entry = bisim.get((name_a, name_b),{})
        if entry.get("different"):
            return
        bisim[(name_a, name_b)] = dict(entry, **{"different": True})
        also_different = entry.get("different_if", [])
        for a, b in also_different:
            mark_as_different(a, b)

    def register_dependency(name_a: str, name_b: str, dep_a: str, dep_b: str):
        pos = (name_a, name_b) if name_a < name_b else (name_b, name_a)
        entry = bisim.get(pos, {})
        deps = entry.get("different_if", [])
        deps.append((dep_a, dep_b))
        bisim[pos] = dict(entry, **{"different_if": deps})


    names = sorted(repos.keys())
    for j in range(len(names)):
        b = names[j]
        for i in range(j):
            a = names[i]
            if is_different(a,b):
                continue
            if not local_repos_equal(repos, names[i], names[j]):
                mark_as_different(names[i], names[j])
                continue
            links_a = repos[a].get("bindings", {})
            links_b = repos[b].get("bindings", {})
            for link in links_a.keys():
                next_a = links_a[link]
                next_b = links_b[link]
                if next_a != next_b:
                    if is_different(next_a, next_b):
                        mark_as_different(a,b)
                        continue
                    else:
                        register_dependency(next_a, next_b, a, b)
    classes = []
    done = {}
    for j in reversed(range(len(names))):
        name_j = names[j]
        if done.get(name_j):
            continue
        c = [name_j]
        for i in range(j):
            name_i = names[i]
            if not bisim.get((name_i, name_j),{}).get("different"):
                c.append(name_i)
                done[name_i] = True
        classes.append(c)
    return classes

def dedup(repos: Json, user_keep: List[str]) -> Json:

    keep = set(user_keep)
    main = repos.get("main")
    if isinstance(main, str):
        keep.add(main)

    def choose_representative(c: List[str]) -> str:
        """Out of a bisimilarity class chose a main representative"""
        candidates = c
        # Keep a repository with a proper root, if any of those has a root.
        # In this way, we're not losing actual roots.
        with_root = [ n for n in candidates
                      if isinstance(repos["repositories"][n]["repository"],
                                    dict)]
        if with_root:
            candidates = with_root

        # Prefer to choose a repository we have to keep anyway
        keep_entries = set(candidates) & keep
        if keep_entries:
            candidates = list(keep_entries)

        return sorted(candidates,
                      key=lambda s: (s.count("/"), len(s), s))[0]

    bisim = bisimilar_repos(repos["repositories"])
    renaming = {}
    for c in bisim:
        if len(c) == 1:
            continue
        rep = choose_representative(c)
        for repo in c:
            if ((repo not in keep) and (repo != rep)):
                renaming[repo] = rep

    def final_root_reference(name: str) -> str:
        """For a given repository name, return a name than can be used
        to name root in the final repository configuration."""
        root: Json = repos["repositories"][name]["repository"]
        if isinstance(root, dict):
            # actual root; can still be merged into a different once, but only
            # one with a proper root as well.
            return renaming.get(name, name)
        elif isinstance(root, str):
            return final_root_reference(root)
        else:
            fail("Invalid root found for %r: %r" % (name, root))

    new_repos = {}
    for name in repos["repositories"].keys():
        if name not in renaming:
            desc = repos["repositories"][name]
            if "bindings" in desc:
                bindings = desc["bindings"]
                new_bindings = {}
                for k, v in bindings.items():
                    if v in renaming:
                        new_bindings[k] = renaming[v]
                    else:
                        new_bindings[k] = v
                desc = dict(desc, **{"bindings": new_bindings})
            new_roots: Json = {}
            for root in ["repository", "target_root", "rule_root"]:
                root_val: Json = desc.get(root)
                if isinstance(root_val, str) and (root_val in renaming):
                    new_roots[root] = final_root_reference(root_val)
            desc = dict(desc, **new_roots)
            new_repos[name] = desc
    return dict(repos, **{"repositories": new_repos})

if __name__ == "__main__":
    orig = json.load(sys.stdin)
    final = dedup(orig, sys.argv[1:])
    print(json.dumps(final))
