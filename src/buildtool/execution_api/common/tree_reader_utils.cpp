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

#include "src/buildtool/execution_api/common/tree_reader_utils.hpp"

#include <exception>
#include <type_traits>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace {
[[nodiscard]] auto CreateObjectInfo(HashFunction hash_function,
                                    bazel_re::DirectoryNode const& node)
    -> std::optional<Artifact::ObjectInfo> {
    auto digest = ArtifactDigestFactory::FromBazel(hash_function.GetType(),
                                                   node.digest());
    if (not digest) {
        return std::nullopt;
    }
    return Artifact::ObjectInfo{.digest = *std::move(digest),
                                .type = ObjectType::Tree};
}

[[nodiscard]] auto CreateObjectInfo(HashFunction hash_function,
                                    bazel_re::FileNode const& node)
    -> std::optional<Artifact::ObjectInfo> {
    auto digest = ArtifactDigestFactory::FromBazel(hash_function.GetType(),
                                                   node.digest());
    if (not digest) {
        return std::nullopt;
    }
    return Artifact::ObjectInfo{.digest = *std::move(digest),
                                .type = node.is_executable()
                                            ? ObjectType::Executable
                                            : ObjectType::File};
}

[[nodiscard]] auto CreateObjectInfo(HashFunction hash_function,
                                    bazel_re::SymlinkNode const& node)
    -> Artifact::ObjectInfo {
    return Artifact::ObjectInfo{
        .digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
            hash_function, node.target()),
        .type = ObjectType::Symlink};
}

template <typename TTree>
[[nodiscard]] auto TreeToString(TTree const& entries)
    -> std::optional<std::string> {
    auto json = nlohmann::json::object();
    TreeReaderUtils::InfoStoreFunc store_infos =
        [&json](std::filesystem::path const& path,
                Artifact::ObjectInfo&& info) -> bool {
        static constexpr bool kSizeUnknown =
            std::is_same_v<TTree, GitRepo::tree_entries_t>;

        json[path.string()] = std::move(info).ToString(kSizeUnknown);
        return true;
    };

    if (TreeReaderUtils::ReadObjectInfos(entries, store_infos)) {
        return json.dump(2) + "\n";
    }
    Logger::Log(LogLevel::Error, "reading object infos from Directory failed");
    return std::nullopt;
}

}  // namespace

auto TreeReaderUtils::ReadObjectInfos(bazel_re::Directory const& dir,
                                      InfoStoreFunc const& store_info) noexcept
    -> bool {
    // SHA256 is used since bazel types are processed here.
    HashFunction const hash_function{HashFunction::Type::PlainSHA256};
    try {
        for (auto const& f : dir.files()) {
            auto info = CreateObjectInfo(hash_function, f);
            if (not info or not store_info(f.name(), *std::move(info))) {
                return false;
            }
        }

        for (auto const& l : dir.symlinks()) {
            // check validity of symlinks
            if (not PathIsNonUpwards(l.target())) {
                Logger::Log(
                    LogLevel::Error, "found invalid symlink at {}", l.name());
                return false;
            }
            if (not store_info(l.name(), CreateObjectInfo(hash_function, l))) {
                return false;
            }
        }
        for (auto const& d : dir.directories()) {
            auto info = CreateObjectInfo(hash_function, d);
            if (not info or not store_info(d.name(), *std::move(info))) {
                return false;
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "reading object infos from Directory failed with:\n{}",
                    ex.what());
        return false;
    }
    return true;
}

auto TreeReaderUtils::ReadObjectInfos(GitRepo::tree_entries_t const& entries,
                                      InfoStoreFunc const& store_info) noexcept
    -> bool {
    try {
        for (auto const& [raw_id, es] : entries) {
            auto const hex_id = ToHexString(raw_id);
            for (auto const& entry : es) {
                auto digest =
                    ArtifactDigestFactory::Create(HashFunction::Type::GitSHA1,
                                                  hex_id,
                                                  /*size is unknown*/ 0,
                                                  IsTreeObject(entry.type));
                if (not digest or
                    not store_info(
                        entry.name,
                        Artifact::ObjectInfo{.digest = *std::move(digest),
                                             .type = entry.type})) {
                    return false;
                }
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "reading object infos from Git tree failed with:\n{}",
                    ex.what());
        return false;
    }
    return true;
}

auto TreeReaderUtils::DirectoryToString(bazel_re::Directory const& dir) noexcept
    -> std::optional<std::string> {
    try {
        return TreeToString(dir);
    } catch (const std::exception& e) {
        Logger::Log(LogLevel::Error,
                    "An error occurred while reading bazel:re::Directory:\n",
                    e.what());
        return std::nullopt;
    }
}

auto TreeReaderUtils::GitTreeToString(
    GitRepo::tree_entries_t const& entries) noexcept
    -> std::optional<std::string> {
    try {
        return TreeToString(entries);
    } catch (const std::exception& e) {
        Logger::Log(LogLevel::Error,
                    "An error occurred while reading git tree:\n{}",
                    e.what());
        return std::nullopt;
    }
}
