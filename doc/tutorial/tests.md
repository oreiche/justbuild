Creating Tests
==============

To run tests with *justbuild*, we do **not** have a dedicated `test`
subcommand. Instead, we consider tests being a specific action that
generates a test report. Consequently, we use the `build` subcommand to
build the test report, and thereby run the test action. Test actions,
however, are slightly different from normal actions in that we don't
want the build of the test report to be aborted if a test action fails
(but still, we want only successfully actions taken from cache). Rules
defining targets containing such special actions have to identify
themselves as *tainted* by specifying a string explaining why such
special actions are justified; in our case, the string is `"test"`.
Besides the implicit marking by using a tainted rule, those tainting
strings can also be explicitly assigned by the user in the definition of
a target, e.g., to mark test data. Any target has to be tainted with (at
least) all the strings any of its dependencies is tainted with. In this
way, it is ensured that no test target will end up in a production
build.

For the remainder of this section, we expect to have the project files
available resulting from successfully completing the tutorial section on
[*Building C++ Hello World*](./hello-world.md). We will demonstrate how
to write a test binary for the `greet` library and a shell test for the
`helloworld` binary.

Creating a C++ test binary
--------------------------

First, we will create a C++ test binary for testing the correct
functionality of the `greet` library. Therefore, we need to provide
a C++ source file that performs the actual testing and returns
non-`0` on failure. For simplicity reasons, we do not use a testing
framework for this tutorial and place the tests into the same logical
repository as the main project; when using additional third-party
code for tests (like a test framework) it is advisable to put tests
in a separate logical repository so that third-party test code is
only fetched when testing is requested. Let us place this code in
subdirectory `tests`

``` sh
mkdir -p ./tests
```

A simple test that captures standard output and verifies it with the expected
output should be provided in the file `tests/greet.test.cpp`:

``` {.cpp srcname="tests/greet.test.cpp"}
#include <functional>
#include <iostream>
#include <string>
#include <unistd.h>
#include "greet/greet.hpp"

template <std::size_t kMaxBufSize = 1024>
auto capture_stdout(std::function<void()> const& func) -> std::string {
  int fd[2];
  if (pipe(fd) < 0) return {};
  int fd_stdout = dup(fileno(stdout));
  fflush(stdout);
  dup2(fd[1], fileno(stdout));

  func();
  fflush(stdout);

  std::string buf(kMaxBufSize, '\0');
  auto n = read(fd[0], &(*buf.begin()), kMaxBufSize);
  close(fd[0]);
  close(fd[1]);
  dup2(fd_stdout, fileno(stdout));
  return buf.substr(0, n);
}

auto test_greet(std::string const& name) -> bool {
  auto expect = std::string{"Hello "} + name + "!\n";
  auto result = capture_stdout([&name] { greet(name); });
  std::cout << "greet output: " << result << std::endl;
  return result == expect;
}

int main() {
  return test_greet("World") && test_greet("Universe") ? 0 : 1;
}
```

Next, a new test target needs to be created in module `greet`. This
target uses the rule `["@", "rules", "CC/test", "test"]` and needs to
depend on the `["greet", "greet"]` target. To create the test target,
add the following to `tests/TARGETS`:

``` {.jsonc srcname="tests/TARGETS"}
{ "greet":
  { "type": ["@", "rules", "CC/test", "test"]
  , "name": ["test_greet"]
  , "srcs": ["greet.test.cpp"]
  , "private-deps": [["greet", "greet"]]
  }
}
```

Before we can run the test, a proper default module for `CC/test` must
be provided. By specifying the appropriate target in this module the
default test runner can be overwritten by a different test runner fom
the rule's workspace root. Moreover, all test targets share runner
infrastructure from `shell/test`, e.g., summarizing multiple runs per
test (to detect flakiness) if the configuration variable `RUNS_PER_TEST`
is set. The precise location of those implicit dependencies can be
seen via `just describe`.

``` sh
$ just-mr describe tests greet
```

However, in our case, we want to use the default runner and therefore it
is sufficient to create an empty module. To do so, we create subdirectories

``` sh
$ mkdir -p ./tutorial-defaults/CC/test
$ mkdir -p ./tutorial-defaults/shell/test
```

where we create, respectively, the file `tutorial-defaults/CC/test/TARGETS`
with content

``` {.jsonc srcname="tutorial-defaults/CC/test/TARGETS"}
{}
```

as well as the file `tutorial-defaults/shell/test/TARGETS` with content

``` {.jsonc srcname="tutorial-defaults/shell/test/TARGETS"}
{}
```

and the file `tutorial-defulats/shell` with content

``` {.jsonc srcname="tutorial-defaults/shell/TARGETS"}
{"defaults": {"type": "defaults"}}
```

indicating that we just use defaults for the shell defaults.
Now we can run the test (i.e., build the test result):

``` sh
$ just-mr build tests greet
INFO: Performing repositories setup
INFO: Found 3 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","tests","greet"]
INFO: Requested target is [["@","tutorial","tests","greet"],{}]
INFO: Analysed target [["@","tutorial","tests","greet"],{}]
INFO: Target tainted ["test"].
INFO: Discovered 5 actions, 3 trees, 1 blobs
INFO: Building [["@","tutorial","tests","greet"],{}].
INFO: Processed 5 actions, 2 cache hits.
INFO: Artifacts built, logical paths are:
        pwd [7cbf5be8541d3e99ae5f427d2633c9c13889d0db:313:f]
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        stderr [8b137891791fe96927ad78e64b0aad7bded08bdc:1:f]
        stdout [ae6c6813755da67954a4a562f6d2ef01578c3e89:60:f]
        time-start [329c969563b282c6eea8774edddad6b40f568fe9:11:f]
        time-stop [329c969563b282c6eea8774edddad6b40f568fe9:11:f]
      (1 runfiles omitted.)
INFO: Target tainted ["test"].
$
```

Note that the target is correctly reported as tainted with `"test"`. It
will produce 3 additional actions for compiling, linking and running the
test binary.

The result of the test target is formed of 5 artifacts: `result` (containing
`UNKNOWN`, `PASS`, or `FAIL`), `stderr`, `stdout`, `time-start`, and
`time-stop`, and a single runfile (omitted in the output above), which
is a tree artifact with the name `test_greet` that contains all of the
above artifacts. The test was run successfully as otherwise all reported
artifacts would have been reported as `FAILED` in the output, and
*justbuild* would have returned the exit code `2`.

To immediately print the standard output produced by the test binary on
the command line, the `-P` option can be used. Argument to this option
is the name of the artifact that should be printed on the command line,
in our case `stdout`:

``` sh
INFO: Performing repositories setup
INFO: Found 3 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","tests","greet","-P","stdout"]
INFO: Requested target is [["@","tutorial","tests","greet"],{}]
INFO: Analysed target [["@","tutorial","tests","greet"],{}]
INFO: Target tainted ["test"].
INFO: Discovered 5 actions, 3 trees, 1 blobs
INFO: Building [["@","tutorial","tests","greet"],{}].
INFO: Processed 5 actions, 5 cache hits.
INFO: Artifacts built, logical paths are:
        pwd [7cbf5be8541d3e99ae5f427d2633c9c13889d0db:313:f]
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        stderr [8b137891791fe96927ad78e64b0aad7bded08bdc:1:f]
        stdout [ae6c6813755da67954a4a562f6d2ef01578c3e89:60:f]
        time-start [329c969563b282c6eea8774edddad6b40f568fe9:11:f]
        time-stop [329c969563b282c6eea8774edddad6b40f568fe9:11:f]
      (1 runfiles omitted.)
greet output: Hello World!

greet output: Hello Universe!


INFO: Target tainted ["test"].
$
```

To also strip *justbuild*'s `INFO:` prints from this output, the argument
`--log-limit 1` can be passed to the `just-mr` call.

Our test binary does not have any useful options for directly
interact with it. When working with test frameworks, it sometimes can
be desirable to get hold of the test binary itself for manual
interaction. The running of the test binary is the last action
associated with the test and the test binary is, of course, one of its
inputs.

``` sh
$ just-mr analyse --request-action-input -1 tests greet
INFO: Performing repositories setup
INFO: Found 3 repositories to set up
INFO: Setup finished, exec ["just","analyse","-C","...","--request-action-input","-1","tests","greet"]
INFO: Requested target is [["@","tutorial","tests","greet"],{}]
INFO: Request is input of action #-1
INFO: Analysed target [["@","tutorial","tests","greet"],{}]
INFO: Result of input of action #-1 of target [["@","tutorial","tests","greet"],{}]: {
        "artifacts": {
          "runner": {"data":{"file_type":"x","id":"4984b1766a38849c7039f8ae9ede9dae891eebc3","size":2004},"type":"KNOWN"},
          "test": {"data":{"id":"9ed409fe6c4fad2276ad1e1065187ceb4b6fdd41e5aa78709286d3d1ff6f114e","path":"work/test_greet"},"type":"ACTION"},
          "test-args.json": {"data":{"file_type":"f","id":"0637a088a01e8ddab3bf3fa98dbe804cbde1a0dc","size":2},"type":"KNOWN"},
          "test-launcher.json": {"data":{"file_type":"f","id":"0637a088a01e8ddab3bf3fa98dbe804cbde1a0dc","size":2},"type":"KNOWN"}
        },
        "provides": {
          "cmd": [
            "./runner"
          ],
          "env": {
          },
          "may_fail": "CC test /test_greet failed",
          "output": [
            "pwd",
            "result",
            "stderr",
            "stdout",
            "time-start",
            "time-stop"
          ],
          "output_dirs": [
          ]
        },
        "runfiles": {
        }
      }
INFO: Target tainted ["test"].
$
```

The provided data also shows us the precise description of the action
for which we request the input. This allows us to manually rerun the
action. Or we can simply interact with the test binary manually after
installing the inputs to this action. Requesting the inputs of an action
can also be useful when debugging a build failure.

``` sh
$ just-mr install -o work --request-action-input -1 tests greet
INFO: Performing repositories setup
INFO: Found 3 repositories to set up
INFO: Setup finished, exec ["just","install","-C","...","-o","work","--request-action-input","-1","tests","greet"]
INFO: Requested target is [["@","tutorial","tests","greet"],{}]
INFO: Request is input of action #-1
INFO: Analysed target [["@","tutorial","tests","greet"],{}]
INFO: Target tainted ["test"].
INFO: Discovered 5 actions, 3 trees, 1 blobs
INFO: Building input of action #-1 of [["@","tutorial","tests","greet"],{}].
INFO: Processed 4 actions, 4 cache hits.
INFO: Artifacts can be found in:
        /tmp/tutorial/work/runner [4984b1766a38849c7039f8ae9ede9dae891eebc3:2004:x]
        /tmp/tutorial/work/test [306e9440ba06bf615f51d84cde0ce76563723c3d:24448:x]
        /tmp/tutorial/work/test-args.json [0637a088a01e8ddab3bf3fa98dbe804cbde1a0dc:2:f]
        /tmp/tutorial/work/test-launcher.json [0637a088a01e8ddab3bf3fa98dbe804cbde1a0dc:2:f]
INFO: Target tainted ["test"].
$ cd work/
$ ./test
greet output: Hello World!

greet output: Hello Universe!

$ echo $?
0
$ cd ..
$ rm -rf work
```

When looking at the reported actions, we also see the difference
between the action graph and the part of the action graph that
is needed to compute the requested artifacts. Targets are always
analyzed completely, including all actions occurring in their
definition. When building, however, only that part of the graph is
traversed that is needed for the requested artifacts. In our case,
the actual test action is not considered in the build, even though
it is part of the definition of the target.

A larger difference between actions discovered in the analysis and
actions processed during the build can occur when rules only use parts
of a target; consider, e.g., the auxiliary target `just-ext-hdrs`
that collects the (partially generated) header files of the external
dependencies, but not the actual libraries. In this case, the
actions for generating those libraries (compiling sources, calling
the archive tool) are discovered when analyzing the target, but
never visited during the build.

Creating a shell test
---------------------

Similarly, to create a shell test for testing the `helloworld` binary, a
test script `tests/test_helloworld.sh` must be provided:

``` {.sh srcname="tests/test_helloworld.sh"}
set -e
[ "$(./helloworld)" = "Hello Universe!" ]
```

The test target for this shell tests uses the rule
`["@", "rules", "shell/test", "script"]` and must depend on the
`"helloworld"` target. To create the test target, add the following to
the `tests/TARGETS` file:

``` {.jsonc srcname="tests/TARGETS"}
...
, "helloworld":
  { "type": ["@", "rules", "shell/test", "script"]
  , "name": ["test_helloworld"]
  , "test": ["test_helloworld.sh"]
  , "deps": [["", "helloworld"]]
  }
...
```

A shell test depends on the default settings for the shell. Therefore,
if we bring our own toolchain defaults for our rules, we have to do this
here as well. In this case, however, we have already created the toolchain
description before running the C++ test, as that also uses the shell toolchain
for the summarizing results.

Now we can run the shell test (i.e., build the test result):

``` sh
$ just-mr build tests helloworld
INFO: Performing repositories setup
INFO: Found 3 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","tests","helloworld"]
INFO: Requested target is [["@","tutorial","tests","helloworld"],{}]
INFO: Analysed target [["@","tutorial","tests","helloworld"],{}]
INFO: Target tainted ["test"].
INFO: Discovered 5 actions, 4 trees, 2 blobs
INFO: Building [["@","tutorial","tests","helloworld"],{}].
INFO: Processed 5 actions, 4 cache hits.
INFO: Artifacts built, logical paths are:
        pwd [c9deab746798a2d57873d6d05e8a7ecff21acf9e:313:f]
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        stderr [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        stdout [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        time-start [b05cdf005874e63ea5a6d1242acbbd64cef0e339:11:f]
        time-stop [b05cdf005874e63ea5a6d1242acbbd64cef0e339:11:f]
      (1 runfiles omitted.)
INFO: Target tainted ["test"].
$
```

The result is also similar, containing also the 5 artifacts and a single
runfile (omitted in the output above), which is a tree artifact with the
name `test_helloworld` that contains all of the above artifacts.

Creating a compound test target
-------------------------------

As most people probably do not want to call every test target by hand,
it is desirable to compound test target that triggers the build of
multiple test reports. To do so, an `"install"` target can be used. The
field `"deps"` of an install target is a list of targets for which the
runfiles are collected. As for the tests the runfiles happen to be tree
artifacts named the same way as the test and containing all test
results, this is precisely what we need. Furthermore, as the dependent
test targets are tainted by `"test"`, also the compound test target must
be tainted by the same string. To create the compound test target
combining the two tests above (the tests `"greet"` and `"helloworld"`
from module `"tests"`), add the following to the `tests/TARGETS` file:

``` {.jsonc srcname="tests/TARGETS"}
...
, "ALL":
  { "type": "install"
  , "tainted": ["test"]
  , "deps": ["greet", "helloworld"]
  }
...
```

Now we can run all tests at once by just building the compound test
target `"ALL"`:

``` sh
$ just-mr build tests ALL
INFO: Performing repositories setup
INFO: Found 3 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","tests","ALL"]
INFO: Requested target is [["@","tutorial","tests","ALL"],{}]
INFO: Analysed target [["@","tutorial","tests","ALL"],{}]
INFO: Target tainted ["test"].
INFO: Discovered 8 actions, 5 trees, 3 blobs
INFO: Building [["@","tutorial","tests","ALL"],{}].
INFO: Processed 8 actions, 8 cache hits.
INFO: Artifacts built, logical paths are:
        test_greet [954aa9658030fe84b297c1d16814d3f04c53b708:208:t]
        test_helloworld [53f7de4084f3ecb12606f5366b65df870af4f7e0:208:t]
INFO: Target tainted ["test"].
$
```

As a result it reports the runfiles (result directories) of both tests
as artifacts. Both tests ran successfully as none of those artifacts in
this output above are tagged as `FAILED`.
