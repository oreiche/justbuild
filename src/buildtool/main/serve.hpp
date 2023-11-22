// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_SERVE_HPP
#define INCLUDED_SRC_BUILDTOOL_MAIN_SERVE_HPP

#ifndef BOOTSTRAP_BUILD_TOOL

#include "gsl/gsl"
#include "src/buildtool/main/cli.hpp"

/// \brief Parse the "just serve" config file.
/// While having a separate config file, almost all fields are already used by
/// "just" itself, so we can populate the respective known command-line fields.
void ReadJustServeConfig(gsl::not_null<CommandLineArguments*> const& clargs);

#endif  // BOOTSTRAP_BUILD_TOOL
#endif  // INCLUDED_SRC_BUILDTOOL_MAIN_SERVE_HPP
