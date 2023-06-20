Creating Tests
==============

To run tests with justbuild, we do *not* have a dedicated `test`
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
*Building C++ Hello World*. We will demonstrate how to write a test
binary for the `greet` library and a shell test for the `helloworld`
binary.

Creating a C++ test binary
--------------------------

First, we will create a C++ test binary for testing the correct
functionality of the `greet` library. Therefore, we need to provide a
C++ source file that performs the actual testing and returns non-`0` on
failure. For simplicity reasons, we do not use a testing framework for
this tutorial. A simple test that captures standard output and verifies
it with the expected output should be provided in the file
`tests/greet.test.cpp`:

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
is set.

However, in our case, we want to use the default runner and therefore it
is sufficient to create an empty module. To do so, create the file
`tutorial-defaults/CC/test/TARGETS` with content

``` {.jsonc srcname="tutorial-defaults/CC/test/TARGETS"}
{}
```

as well as the file `tutorial-defaults/shell/test/TARGETS` with content

``` {.jsonc srcname="tutorial-defaults/shell/test/TARGETS"}
{}
```

Now we can run the test (i.e., build the test result):

``` sh
$ just-mr build tests greet
INFO: Requested target is [["@","tutorial","tests","greet"],{}]
INFO: Analysed target [["@","tutorial","tests","greet"],{}]
INFO: Export targets found: 0 cached, 0 uncached, 0 not eligible for caching
INFO: Target tainted ["test"].
INFO: Discovered 5 actions, 3 trees, 1 blobs
INFO: Building [["@","tutorial","tests","greet"],{}].
INFO: Processed 5 actions, 2 cache hits.
INFO: Artifacts built, logical paths are:
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        stderr [8b137891791fe96927ad78e64b0aad7bded08bdc:1:f]
        stdout [ae6c6813755da67954a4a562f6d2ef01578c3e89:60:f]
        time-start [8e7a68af8d5d7a6d0036c1126ff5c16a5045ae95:11:f]
        time-stop [8e7a68af8d5d7a6d0036c1126ff5c16a5045ae95:11:f]
      (1 runfiles omitted.)
INFO: Target tainted ["test"].
$
```

Note that the target is correctly reported as tainted with `"test"`. It
will produce 3 additional actions for compiling, linking and running the
test binary.

The result of the test target are 5 artifacts: `result` (containing
`UNKNOWN`, `PASS`, or `FAIL`), `stderr`, `stdout`, `time-start`, and
`time-stop`, and a single runfile (omitted in the output above), which
is a tree artifact with the name `test_greet` that contains all of the
above artifacts. The test was run successfully as otherwise all reported
artifacts would have been reported as `FAILED` in the output, and
justbuild would have returned the exit code `2`.

To immediately print the standard output produced by the test binary on
the command line, the `-P` option can be used. Argument to this option
is the name of the artifact that should be printed on the command line,
in our case `stdout`:

``` sh
$ just-mr build tests greet --log-limit 1 -P stdout
greet output: Hello World!

greet output: Hello Universe!

$
```

Note that `--log-limit 1` was just added to omit justbuild's `INFO:`
prints.

Our test binary does not have any useful options for directly
interacting with it. When working with test frameworks, it sometimes can
be desirable to get hold of the test binary itself for manual
interaction. The running of the test binary is the last action
associated with the test and the test binary is, of course, one of its
inputs.

``` sh
$ just-mr analyse --request-action-input -1 tests greet
INFO: Requested target is [["@","tutorial","tests","greet"],{}]
INFO: Request is input of action #-1
INFO: Result of input of action #-1 of target [["@","tutorial","tests","greet"],{}]: {
        "artifacts": {
          "runner": {"data":{"file_type":"x","id":"0647621fba9b22f0727fbef98104f3e398496e2f","size":1876},"type":"KNOWN"},
          "test": {"data":{"id":"465b690f0b006553c15fb059e2293011c20f74d4","path":"test_greet"},"type":"ACTION"},
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
INFO: Requested target is [["@","tutorial","tests","greet"],{}]
INFO: Request is input of action #-1
INFO: Analysed target [["@","tutorial","tests","greet"],{}]
INFO: Export targets found: 0 cached, 0 uncached, 0 not eligible for caching
INFO: Target tainted ["test"].
INFO: Discovered 5 actions, 3 trees, 1 blobs
INFO: Building input of action #-1 of [["@","tutorial","tests","greet"],{}].
INFO: Processed 4 actions, 4 cache hits.
INFO: Artifacts can be found in:
        /tmp/tutorial/work/runner [0647621fba9b22f0727fbef98104f3e398496e2f:1876:x]
        /tmp/tutorial/work/test [00458536b165abdee1802e5fb7b0922e04c81491:20352:x]
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

Now we can run the shell test (i.e., build the test result):

``` sh
$ just-mr build tests helloworld
INFO: Requested target is [["@","tutorial","tests","helloworld"],{}]
INFO: Analysed target [["@","tutorial","tests","helloworld"],{}]
INFO: Export targets found: 0 cached, 0 uncached, 0 not eligible for caching
INFO: Target tainted ["test"].
INFO: Discovered 5 actions, 4 trees, 0 blobs
INFO: Building [["@","tutorial","tests","helloworld"],{}].
INFO: Processed 5 actions, 4 cache hits.
INFO: Artifacts built, logical paths are:
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        stderr [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        stdout [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        time-start [c31e92e6b16dacec4ee95beefcc6a688dbffee2d:11:f]
        time-stop [c31e92e6b16dacec4ee95beefcc6a688dbffee2d:11:f]
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
INFO: Requested target is [["@","tutorial","tests","ALL"],{}]
INFO: Analysed target [["@","tutorial","tests","ALL"],{}]
INFO: Export targets found: 0 cached, 0 uncached, 0 not eligible for caching
INFO: Target tainted ["test"].
INFO: Discovered 8 actions, 5 trees, 1 blobs
INFO: Building [["@","tutorial","tests","ALL"],{}].
INFO: Processed 8 actions, 8 cache hits.
INFO: Artifacts built, logical paths are:
        test_greet [251ba2038ccdb8ba1ae2f4e963751b9230b36646:177:t]
        test_helloworld [63fa5954161b52b275b05c270e1626feaa8e178b:177:t]
INFO: Target tainted ["test"].
$
```

As a result it reports the runfiles (result directories) of both tests
as artifacts. Both tests ran successfully as none of those artifacts in
this output above are tagged as `FAILED`.
