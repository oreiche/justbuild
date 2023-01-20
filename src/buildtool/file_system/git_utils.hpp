// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_UTILS_HPP

#include "gsl-lite/gsl-lite.hpp"

extern "C" {
struct git_odb;
struct git_repository;
struct git_tree;
struct git_treebuilder;
struct git_index;
struct git_strarray;
struct git_signature;
struct git_object;
struct git_remote;
struct git_commit;
struct git_tree_entry;
}

void repo_closer(gsl::owner<git_repository*> repo);

void odb_closer(gsl::owner<git_odb*> odb);

void tree_closer(gsl::owner<git_tree*> tree);

void treebuilder_closer(gsl::owner<git_treebuilder*> builder);

void index_closer(gsl::owner<git_index*> index);

// to be used for strarrays allocated by libgit2
void strarray_closer(gsl::owner<git_strarray*> strarray);

// to be used for strarrays allocated manually
void strarray_deleter(gsl::owner<git_strarray*> strarray);

void signature_closer(gsl::owner<git_signature*> signature);

void object_closer(gsl::owner<git_object*> object);

void remote_closer(gsl::owner<git_remote*> remote);

void commit_closer(gsl::owner<git_commit*> commit);

void tree_entry_closer(gsl::owner<git_tree_entry*> tree_entry);

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_UTILS_HPP
