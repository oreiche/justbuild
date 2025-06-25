// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/execution_api/local/local_cas_reader.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <deque>
#include <exception>
#include <memory>
#include <stack>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/incremental_reader.hpp"
#include "src/utils/cpp/path.hpp"

namespace {
[[nodiscard]] auto AssembleTree(
    bazel_re::Directory root,
    std::unordered_map<ArtifactDigest, bazel_re::Directory> directories)
    -> bazel_re::Tree;
}  // namespace

auto LocalCasReader::ReadDirectory(ArtifactDigest const& digest) const noexcept
    -> std::optional<bazel_re::Directory> {
    if (auto const path = cas_.TreePath(digest)) {
        if (auto const content = FileSystemManager::ReadFile(*path)) {
            return BazelMsgFactory::MessageFromString<bazel_re::Directory>(
                *content);
        }
    }
    Logger::Log(
        LogLevel::Error, "Directory {} not found in CAS", digest.hash());
    return std::nullopt;
}

auto LocalCasReader::MakeTree(ArtifactDigest const& root) const noexcept
    -> std::optional<bazel_re::Tree> {
    auto const hash_type = cas_.GetHashFunction().GetType();
    try {
        std::unordered_map<ArtifactDigest, bazel_re::Directory> directories;

        std::stack<ArtifactDigest> to_check;
        to_check.push(root);
        while (not to_check.empty()) {
            auto current = to_check.top();
            to_check.pop();

            if (directories.contains(current)) {
                continue;
            }

            auto read_dir = ReadDirectory(current);
            if (not read_dir) {
                return std::nullopt;
            }
            for (auto const& node : read_dir->directories()) {
                auto digest =
                    ArtifactDigestFactory::FromBazel(hash_type, node.digest());
                if (not digest) {
                    return std::nullopt;
                }
                to_check.push(*std::move(digest));
            }
            directories.insert_or_assign(std::move(current),
                                         *std::move(read_dir));
        }
        auto root_directory = directories.extract(root).mapped();
        return AssembleTree(std::move(root_directory), std::move(directories));
    } catch (...) {
        return std::nullopt;
    }
}

auto LocalCasReader::ReadGitTree(ArtifactDigest const& digest) const noexcept
    -> std::optional<GitRepo::tree_entries_t> {
    if (auto const path = cas_.TreePath(digest)) {
        if (auto const content = FileSystemManager::ReadFile(*path)) {
            auto check_symlinks =
                [&cas = cas_](std::vector<ArtifactDigest> const& ids) {
                    for (auto const& id : ids) {
                        auto link_path = cas.BlobPath(id,
                                                      /*is_executable=*/false);
                        if (not link_path) {
                            return false;
                        }
                        // in the local CAS we store as files
                        auto content = FileSystemManager::ReadFile(*link_path);
                        if (not content or not PathIsNonUpwards(*content)) {
                            return false;
                        }
                    }
                    return true;
                };

            // Git-SHA1 hashing is used for reading from git.
            HashFunction const hash_function{HashFunction::Type::GitSHA1};
            return GitRepo::ReadTreeData(*content,
                                         digest.hash(),
                                         check_symlinks,
                                         /*is_hex_id=*/true);
        }
    }
    Logger::Log(LogLevel::Debug, "Tree {} not found in CAS", digest.hash());
    return std::nullopt;
}

auto LocalCasReader::DumpRawTree(Artifact::ObjectInfo const& info,
                                 DumpCallback const& dumper) const noexcept
    -> bool {
    auto path = cas_.TreePath(info.digest);
    return path ? DumpRaw(*path, dumper) : false;
}

auto LocalCasReader::DumpBlob(Artifact::ObjectInfo const& info,
                              DumpCallback const& dumper) const noexcept
    -> bool {
    auto path = cas_.BlobPath(info.digest, IsExecutableObject(info.type));
    return path ? DumpRaw(*path, dumper) : false;
}

auto LocalCasReader::StageBlobTo(
    Artifact::ObjectInfo const& info,
    std::filesystem::path const& output) const noexcept -> bool {
    if (not IsBlobObject(info.type)) {
        return false;
    }
    auto const blob_path =
        cas_.BlobPath(info.digest, IsExecutableObject(info.type));
    if (not blob_path) {
        return false;
    }
    return FileSystemManager::CreateDirectory(output.parent_path()) and
           FileSystemManager::CopyFileAs</*kSetEpochTime=*/true,
                                         /*kSetWritable=*/true>(
               *blob_path, output, info.type);
}

auto LocalCasReader::DumpRaw(std::filesystem::path const& path,
                             DumpCallback const& dumper) noexcept -> bool {
    constexpr std::size_t kChunkSize{512};
    auto to_read = IncrementalReader::FromFile(kChunkSize, path);
    if (not to_read.has_value()) {
        return false;
    }

    return std::all_of(
        to_read->begin(), to_read->end(), [&dumper](auto const& chunk) {
            return chunk.has_value() and
                   std::invoke(dumper, std::string{chunk.value()});
        });
}

auto LocalCasReader::IsNativeProtocol() const noexcept -> bool {
    return ProtocolTraits::IsNative(cas_.GetHashFunction().GetType());
}

auto LocalCasReader::IsDirectoryValid(ArtifactDigest const& digest)
    const noexcept -> expected<bool, std::string> {
    try {
        auto const hash_type = cas_.GetHashFunction().GetType();
        // read directory
        if (auto const path = cas_.TreePath(digest)) {
            if (auto const content = FileSystemManager::ReadFile(*path)) {
                auto dir =
                    BazelMsgFactory::MessageFromString<bazel_re::Directory>(
                        *content);
                // check symlinks
                for (auto const& l : dir->symlinks()) {
                    if (not PathIsNonUpwards(l.target())) {
                        return false;
                    }
                }
                // traverse subdirs
                for (auto const& d : dir->directories()) {
                    auto subdir_digest =
                        ArtifactDigestFactory::FromBazel(hash_type, d.digest());
                    if (not subdir_digest) {
                        return unexpected{std::move(subdir_digest).error()};
                    }
                    auto subdir_result =
                        IsDirectoryValid(*std::move(subdir_digest));
                    if (not subdir_result) {
                        return unexpected{std::move(subdir_result).error()};
                    }
                    if (not std::move(subdir_result).value()) {
                        return false;
                    }
                }
                return true;
            }
        }
        return unexpected{
            fmt::format("Directory {} not found in CAS", digest.hash())};
    } catch (std::exception const& ex) {
        return unexpected{fmt::format(
            "An error occurred checking validity of bazel directory:\n{}",
            ex.what())};
    }
}

namespace {
[[nodiscard]] auto AssembleTree(
    bazel_re::Directory root,
    std::unordered_map<ArtifactDigest, bazel_re::Directory> directories)
    -> bazel_re::Tree {
    using Pair = std::pair<ArtifactDigest const, bazel_re::Directory>;
    std::vector<Pair*> sorted;
    sorted.reserve(directories.size());
    for (auto& p : directories) {
        sorted.push_back(&p);
    }
    std::sort(sorted.begin(), sorted.end(), [](Pair const* l, Pair const* r) {
        return l->first.hash() < r->first.hash();
    });

    ::bazel_re::Tree result{};
    (*result.mutable_root()) = std::move(root);
    result.mutable_children()->Reserve(gsl::narrow<int>(sorted.size()));
    std::transform(sorted.begin(),
                   sorted.end(),
                   ::pb::back_inserter(result.mutable_children()),
                   [](Pair* p) { return std::move(p->second); });
    return result;
}
}  // namespace
