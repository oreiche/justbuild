Building C++ Hello World
========================

*justbuild* is a true language-agnostic (there are no more-equal
languages) and multi-repository build system. As a consequence,
high-level concepts (e.g., C++ binaries, C++ libraries, etc.) are not
hardcoded built-ins of the tool, but rather provided via a set of rules.
These rules can be specified as a true dependency to your project like
any other external repository your project might depend on.

Setting up the Multi-Repository Configuration
---------------------------------------------

To build a project with multi-repository dependencies, we first need to
provide a configuration that declares the required repositories. Before
we begin, we need to declare where the root of our workspace is located
by creating an empty file `ROOT`:

``` sh
$ touch ROOT
```

Second, we also need to create the multi-repository configuration
`repos.json` in the workspace root:

``` {.jsonc srcname="repos.json"}
{ "main": "tutorial"
, "repositories":
  { "rules-cc":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "307c96681e6626286804c45273082dff94127878"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
    }
  , "tutorial":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"rules": "rules-cc"}
    }
  }
}
```

In that configuration, two repositories are defined:

1. The `"rules-cc"` repository located in the subdirectory `rules` of
   [just-buildsystem/rules-cc:307c96681e6626286804c45273082dff94127878](https://github.com/just-buildsystem/rules-cc/tree/307c96681e6626286804c45273082dff94127878),
   which contains the high-level concepts for building C/C++ binaries
   and libraries.

2. The `"tutorial"` repository located at `.`, which contains the
   targets that we want to build. It has a single dependency, which is
   the *rules* that are needed to build the target. These rules are
   bound via the open name `"rules"` to the just created repository
   `"rules-cc"`. In this way, the entities provided by `"rules-cc"` can
   be accessed from within the `"tutorial"` repository via the
   fully-qualified name `["@", "rules", "<module>", "<name>"]`;
   fully-qualified names (for rules, targets to build (like libraries,
   binaries), etc) are given by a repository name, a path specifying a
   directory within that repository (the "module") where the
   specification file is located, and a symbolic name (i.e., an
   arbitrary string that is used as key in the specification).

The final repository configuration contains a single `JSON` object with
the key `"repositories"` referring to an object of repository names as
keys and repository descriptions as values. For convenience, the main
repository to pick is set to `"tutorial"`.

Description of the helloworld target
------------------------------------

For this tutorial, we want to create a target `helloworld` that produces
a binary from the C++ source `main.cpp`. To define such a target, create
a `TARGETS` file with the following content:

``` {.jsonc srcname="TARGETS"}
{ "helloworld":
  { "type": ["@", "rules", "CC", "binary"]
  , "name": ["helloworld"]
  , "srcs": ["main.cpp"]
  }
}
```

The `"type"` field refers to the rule `"binary"` from the module `"CC"`
of the `"rules"` repository. This rule additionally requires the string
field `"name"`, which specifies the name of the binary to produce; as
the generic interface of rules is to have fields either take a list of
strings or a list of targets, we have to specify the name as a list
(this rule will simply concatenate all strings given in this field).
Furthermore, at least one input to the binary is required, which can be
specified via the target fields `"srcs"` or `"deps"`. In our case, the
former is used, which contains our single source file. Source files are
also targets, but, as seen in the "Getting Started" section, not the only
ones; instead of naming a source file, we could also have specified a
`"generic"` target generating one (or many) of the sources of our binary.

Now, the last file that is missing is the actual source file `main.cpp`:

``` {.cpp srcname="main.cpp"}
#include <iostream>

int main() {
  std::cout << "Hello world!\n";
  return 0;
}
```

Building the helloworld target
------------------------------

To build the `helloworld` target, we need specify it on the `just-mr`
command line:

``` sh
$ just-mr build helloworld
INFO: Requested target is [["@","tutorial","","helloworld"],{}]
INFO: Analysed target [["@","tutorial","","helloworld"],{}]
INFO: Discovered 2 actions, 1 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{}].
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [bd36255e856ddb72c844c2010a785ab70ee75d56:17088:x]
$
```

Note that the target is taken from the `tutorial` repository, as it
specified as the main repository in `repos.json`. If targets from other
repositories should be build, the repository to use must be specified
via the `--main` option.

`just-mr` reads the repository configuration, fetches externals (if
any), generates the actual build configuration, and stores it in its
cache directory (by default under `$HOME/.cache/just`). Afterwards, the
generated configuration is used to call the `just` binary, which
performs the actual build.

Note that these two programs, `just-mr` and `just`, can also be run
individually. To do so, first run `just-mr` with `setup` and capture the
path to the generated build configuration from stdout by assigning it to
a shell variable (e.g., `CONF`). Afterwards, `just` can be called to
perform the actual build by explicitly specifying the configuration file
via `-C`:

``` sh
$ CONF=$(just-mr setup tutorial)
$ just build -C $CONF helloworld
```

Note that `just-mr` only needs to be run the very first time and only
once again whenever the `repos.json` file is modified.

By default, the BSD-default compiler front-ends (which are also defined
for most Linux distributions) `cc` and `c++` are used for C and C++
(variables `"CC"` and `"CXX"`). If you want to temporarily use different
defaults, you can use `-D` to provide a JSON object that sets different
default variables. For instance, to use Clang as C++ compiler for a
single build invocation, you can use the following command to provide an
object that sets `"CXX"` to `"clang++"`:

``` sh
$ just-mr build helloworld -D'{"CXX":"clang++"}'
INFO: Requested target is [["@","tutorial","","helloworld"],{"CXX":"clang++"}]
INFO: Analysed target [["@","tutorial","","helloworld"],{"CXX":"clang++"}]
INFO: Discovered 2 actions, 1 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{"CXX":"clang++"}].
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [a1e0dc77ec6f171e118a3e6992859f68617a2c6f:16944:x]
$
```

Defining project defaults
-------------------------

To define a custom set of defaults (toolchain and compile flags) for
your project, you need to create a separate file root for providing
required `TARGETS` file, which contains the `"defaults"` target that
should be used by the rules. This file root is then used as the *target
root* for the rules, i.e., the search path for `TARGETS` files. In this
way, the description of the `"defaults"` target, while logically part
of the rules repository is physically located in a separate directory
to keep the rules repository independent of these project-specific
definitions.

We will call the new file root `tutorial-defaults` and need to create a
module directory `CC` in it:

``` sh
$ mkdir -p ./tutorial-defaults/CC
```

In that module, we need to create the file
`tutorial-defaults/CC/TARGETS` that contains the target `"defaults"` and
specifies which toolchain and compile flags to use; it has to specify
the complete toolchain, but can specify a `"base"` toolchain to inherit
from. In our case, we don't use any base, but specify all the required
fields directly.

``` {.jsonc srcname="tutorial-defaults/CC/TARGETS"}
{ "defaults":
  { "type": ["CC", "defaults"]
  , "CC": ["cc"]
  , "CXX": ["c++"]
  , "CFLAGS": ["-O2", "-Wall"]
  , "CXXFLAGS": ["-O2", "-Wall"]
  , "AR": ["ar"]
  , "PATH": ["/bin", "/usr/bin"]
  }
}
```

To use the project defaults, modify the existing `repos.json` to reflect
the following content:

``` {.jsonc srcname="repos.json"}
{ "main": "tutorial"
, "repositories":
  { "rules-cc":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "307c96681e6626286804c45273082dff94127878"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
    , "target_root": "tutorial-defaults"
    , "rule_root": "rules-cc"
    }
  , "tutorial":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"rules": "rules-cc"}
    }
  , "tutorial-defaults":
    { "repository": {"type": "file", "path": "./tutorial-defaults"}
    }
  }
}
```

Note that the `"defaults"` target uses the rule `["CC", "defaults"]`
without specifying any external repository (e.g.,
`["@", "rules", ...]`). This is because `"tutorial-defaults"` is not a
full-fledged repository but merely a file root that is considered local
to the `"rules-cc"` repository. In fact, the `"rules-cc"` repository
cannot refer to any external repository as it does not have any defined
bindings. The naming for rules follows the same scheme we've already
seen for targets, so a single string refers to an entity in the same
directory. As our `"defaults"` target is in the directory `"CC"` of
the rules repository we could also have written the rule `"type"`
simply as `"defaults"`.

To rebuild the project, we need to rerun `just-mr` (note that due to
configuration changes, rerunning only `just` would not suffice):

``` sh
$ just-mr build helloworld
INFO: Requested target is [["@","tutorial","","helloworld"],{}]
INFO: Analysed target [["@","tutorial","","helloworld"],{}]
INFO: Discovered 2 actions, 1 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{}].
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [0d5754a83c7c787b1c4dd717c8588ecef203fb72:16992:x]
$
```

Note that the output binary may have changed due to different defaults.

In this tutorial we simply set the correct parameters of the defaults target.
It is, however, not necessary to remember all the fields of a rule; we can
always ask `just` to present us the available field names and configuration
variables together with any documentation the rule author provided. For
this, we use the `describe` subcommand; as we're interested in a target of
the `rules-cc` repository, which is not the default repository, we also
have to specify the repository name.

``` sh
$ just-mr --main rules-cc describe CC defaults
```

Of course, the `describe` subcommand works generically on all
targets. For example, by asking to describe our `helloworld` target,
we will get reminded about all the various fields and relevant
configuration variables of a C++ binary.

``` sh
$ just-mr describe helloworld
```


Modeling target dependencies
----------------------------

For demonstration purposes, we will separate the print statements into a
static library `greet`, which will become a dependency to our binary.
Therefore, we create a new subdirectory `greet` with the files
`greet/greet.hpp`:

``` {.cpp srcname="greet/greet.hpp"}
#include <string>

void greet(std::string const& s);
```

and `greet/greet.cpp`:

``` {.cpp srcname="greet/greet.cpp"}
#include "greet.hpp"
#include <iostream>

void greet(std::string const& s) {
  std::cout << "Hello " << s << "!\n";
}
```

These files can now be used to create a static library `libgreet.a`. To
do so, we need to create the following target description in
`greet/TARGETS`:

``` {.jsonc srcname="greet/TARGETS"}
{ "greet":
  { "type": ["@", "rules", "CC", "library"]
  , "name": ["greet"]
  , "hdrs": ["greet.hpp"]
  , "srcs": ["greet.cpp"]
  , "stage": ["greet"]
  }
}
```

Similar to `"binary"`, we have to provide a name and source file.
Additionally, a library has public headers defined via `"hdrs"` and an
optional staging directory `"stage"` (default value `"."`). The staging
directory specifies where the consumer of this library can expect to
find the library's artifacts. Note that this does not need to reflect
the location on the file system (i.e., a full-qualified path like
`["com", "example", "utils", "greet"]` could be used to distinguish it
from greeting libraries of other projects). The staging directory does
not only affect the main artifact `libgreet.a` but also it's
*runfiles*, a second set of artifacts, usually those a consumer needs to
make proper use the actual artifact; in the case of a library, the
runfiles are its public headers. Hence, the public header will be staged
to `"greet/greet.hpp"`. With that knowledge, we can now perform the
necessary modifications to `main.cpp`:

``` {.cpp srcname="main.cpp"}
#include "greet/greet.hpp"

int main() {
  greet("Universe");
  return 0;
}
```

The target `"helloworld"` will have a direct dependency to the target
`"greet"` of the module `"greet"` in the top-level `TARGETS` file:

``` {.jsonc srcname="TARGETS"}
{ "helloworld":
  { "type": ["@", "rules", "CC", "binary"]
  , "name": ["helloworld"]
  , "srcs": ["main.cpp"]
  , "private-deps": [["greet", "greet"]]
  }
}
```

Note that there is no need to explicitly specify `"greet"`'s public
headers here as the appropriate artifacts of dependencies are
automatically added to the inputs of compile and link actions. The new
binary can be built with the same command as before (no need to rerun
`just-mr`):

``` sh
$ just-mr build helloworld
INFO: Requested target is [["@","tutorial","","helloworld"],{}]
INFO: Analysed target [["@","tutorial","","helloworld"],{}]
INFO: Discovered 4 actions, 2 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{}].
INFO: Processed 4 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [a0e593e4d52e8b3e14863b3cf1f80809143829ca:17664:x]
$
```

To only build the static library target `"greet"` from module `"greet"`,
run the following command:

``` sh
$ just-mr build greet greet
INFO: Requested target is [["@","tutorial","greet","greet"],{}]
INFO: Analysed target [["@","tutorial","greet","greet"],{}]
INFO: Discovered 2 actions, 1 trees, 0 blobs
INFO: Building [["@","tutorial","greet","greet"],{}].
INFO: Processed 2 actions, 2 cache hits.
INFO: Artifacts built, logical paths are:
        greet/libgreet.a [83ed406e21f285337b0c9bd5011f56f656bba683:2992:f]
      (1 runfiles omitted.)
$
```
