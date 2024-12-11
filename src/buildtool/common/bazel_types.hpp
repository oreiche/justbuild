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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_BAZEL_TYPES_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_BAZEL_TYPES_HPP

/// \file bazel_types.hpp
/// \brief This file contains commonly used aliases for Bazel API
/// Never include this file in any other header!

#ifdef BOOTSTRAP_BUILD_TOOL

#include <cstddef>
#include <cstdint>
#include <string>

namespace build::bazel::remote::execution::v2 {
struct Digest {
    std::string hash_;
    std::int64_t size_bytes_;

    auto hash() const& noexcept -> std::string const& { return hash_; }

    auto size_bytes() const noexcept -> std::int64_t { return size_bytes_; }

    void set_size_bytes(std::int64_t size_bytes) { size_bytes_ = size_bytes; }

    void set_hash(std::string hash) { hash_ = hash; }

    std::string* mutable_hash() { return &hash_; }
};
}  // namespace build::bazel::remote::execution::v2

namespace google::protobuf {
using int64 = std::int64_t;
}

#else

#include <cstddef>
#include <functional>
#include <string>

#include "build/bazel/remote/execution/v2/remote_execution.pb.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_ptr_field.h"

#endif

/// \brief Alias namespace for bazel remote execution
// NOLINTNEXTLINE(misc-unused-alias-decls)
namespace bazel_re = build::bazel::remote::execution::v2;

#ifdef BOOTSTRAP_BUILD_TOOL
// not using protobuffers
#else

/// \brief Alias namespace for 'google::protobuf'
namespace pb {
// NOLINTNEXTLINE(google-build-using-namespace)
using namespace google::protobuf;

/// \brief Alias function for 'RepeatedFieldBackInserter'
template <typename T>
auto back_inserter(RepeatedField<T>* const f) {
    return RepeatedFieldBackInserter(f);
}

/// \brief Alias function for 'RepeatedPtrFieldBackInserter'
template <typename T>
auto back_inserter(RepeatedPtrField<T>* const f) {
    return RepeatedPtrFieldBackInserter(f);
}

}  // namespace pb
#endif

namespace std {

/// \brief Hash function to support bazel_re::Digest as std::map* key.
template <>
struct hash<bazel_re::Digest> {
    auto operator()(bazel_re::Digest const& d) const noexcept -> std::size_t {
        return std::hash<std::string>{}(d.hash());
    }
};

/// \brief Equality function to support bazel_re::Digest as std::map* key.
template <>
struct equal_to<bazel_re::Digest> {
    auto operator()(bazel_re::Digest const& lhs,
                    bazel_re::Digest const& rhs) const noexcept -> bool {
        return lhs.hash() == rhs.hash();
    }
};

}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_BAZEL_TYPES_HPP
