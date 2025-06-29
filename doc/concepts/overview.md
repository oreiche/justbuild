Tool Overview
=============

Structuring
-----------

### Structuring the Build: Targets, Rules, and Actions

The primary units this build system deals with are targets: the user
requests the system to build (or install) a target, targets depend on
other targets, etc. Targets typically reflect the units a software
developer thinks in: libraries, binaries, etc. The definition of a
target only describes the information directly belonging to the target,
e.g., its source, private and public header files, and its direct
dependencies. Any other information needed to build a target (like the
public header files of an indirect dependency) are inferred by the build
tool. In this way, the build description can be kept maintainable

A built target consists of files logically belonging together (like the
actual library file and its public headers) as well as information on
how to use the target (linking arguments, transitive header files, etc).
For a consumer of a target, the definition of this collection of files
as well as the additionally provided information is what defines the
target as a dependency, irrespectively of where the target is coming from
(i.e., targets coinciding here are indistinguishable for other targets).

Of course, to actually build a single target from its dependencies, many
invocations of the compiler or other tools are necessary (so called
"actions"); the build tool translates these high-level description
into the individual actions necessary and only re-executes those where
inputs have changed.

This translation of high-level concepts into individual actions is not
hard coded into the tool. It is provided by the user as "rules" and
forms additional input to the build. To avoid duplicate work, rules are
typically maintained centrally for a project or an organization.

### Structuring the Code: Modules and Repositories

The code base is usually split into many directories, each containing
source files belonging together. To allow the definition of targets
where their code is, the targets are structured in a similar way. For
each directory, there can be a targets file. Directories for which such
a targets file exists are called "modules". Each file belongs to the
module that is closest when searching upwards in the directory tree. The
targets file of a module defines the targets formed from the source
files belonging to this module.

Larger projects are often split into "repositories". For this build
tool, a repository is a logical unit. Often those coincide with the
repositories in the sense of version control. This, however, does not
have to be the case. Also, from one directory in the file system many
repositories can be formed that might differ in the rules used, targets
defined, or binding of their dependencies.

Staging
-------

A peculiarity of this build system is the complete separation between
physical and logical paths. Targets have their own view of the world,
i.e., they can place their artifacts at any logical path they like, and
this is how they look to other targets. It is up to the consuming
targets what they do with artifacts of the targets they depend on; in
particular, they are not obliged to leave them at the logical location
their dependency put them.

When such a collection of artifacts at logical locations (often referred
to as the "stage") is realized on the file system (when installing a
target, or as inputs to actions), the paths are interpreted as paths
relative to the respective root (installation or action directory).

This separation is what allows flexible combination of targets from
various sources without leaking repository names or different file
arrangement if a target is in the "main" repository.

Repository data
---------------

A repository uses a (logical) directory for several purposes: to obtain
source files, to read definitions of targets, to read rules, and to read
expressions that can be used by rules. While all those directories can be
(and often are) the same, this does not have to be the case. For each
of those purposes, a different logical directory (also called "root")
can be used. In this way, one can, e.g., add target definitions to a
source tree originally written for a different build tool without
modifying the original source tree.

Those roots are usually defined in a repository configuration. For the
"main" repository, i.e., the repository from which the target to be
built is requested, the roots can also be overwritten at the command
line. Roots can be defined as paths in the file system, but also as
`git` tree identifiers (together with the location of some repository
containing that tree). The latter definition is preferable for rules and
dependencies, as it allows high-level caching of targets. It also
motivates the need of adding target definitions without changing the
root itself.

The same flexibility as for the roots is also present for the names of
the files defining targets, rules, and expressions. While the default
names `TARGETS`, `RULES`, and `EXPRESSIONS` are often used, other file
names can be specified for those as well, either in the repository
configuration or (for the main repository) on the command line.

The final piece of data needed to describe a repository is the binding
of the open repository names that are used to refer to other
repositories. More details can be found in the documentation on
multi-repository builds.

Targets
-------

### Target naming

In description files, targets, rules, and expressions are referred to by
name. As the context always fixes if a name for a target, rule, or
expression is expected, they use the same naming scheme.

 - A single string refers to the target with this name in the same
   module.
 - A pair `[module, name]` refers to the target `name` in the module
   `module` of the same repository. There are no module names with a
   distinguished meaning. The naming scheme is unambiguous, as all
   other names given by lists have length at least 3.
 - A list `["./", relative-module-path, name]` refers to a target with
   the given name in the module that has the specified path relative to
   the current module (in the current repository).
 - A list `["@", repository, module, name]` refers to the target with
   the specified name in the specified module of the specified
   repository.

Additionally, there are special targets that can also be referred to in
target files.

 - An explicit reference of a source-file target in the same module,
   specified as `["FILE", null, name]`. The explicit `null` at the
   second position (where normally the module would be) is necessary to
   ensure the name has length more than 2 to distinguish it from a
   reference to the module `"FILE"`.
 - An explicit reference of a non-upwards symlink target in the same module,
   specified as `["SYMLINK", null, name]`. The explicit `null` at the
   second position is required for the same reason as in the explicit
   file reference. It is the user's responsibility to ensure the symlink
   pointed to is non-upwards.
 - A reference to an collection, given by a shell pattern, of explicit
   source files in the top-level directory of the same module,
   specified as `["GLOB", null, pattern]`. The explicit `null` at
   second position is required for the same reason as in the explicit
   file reference.
 - A reference to a tree target in the same module, specified as
   `["TREE", null, name]`. The explicit `null` at second position is
   required for the same reason as in the explicit file reference.

### Data of an analyzed target

Analyzing a target results in 3 pieces of data.

 - The "artifacts" are a staged collection of artifacts. Typically,
   these are what is normally considered the main reason to build a
   target, e.g., the actual library file in case of a library.

 - The "runfiles" are another staged collection of artifacts.
   Typically, these are files that directly belong to the target and
   are somehow needed to use the target. For example, in case of a
   library that would be the public header files of the library itself.

 - A "provides" map with additional information the target wants to
   provide to its consumers. The data contained in that map can also
   contain additional artifacts. Typically, this the remaining
   information needed to use the target in a build.

   In case of a library, that typically would include any other
   libraries this library transitively depends upon (a stage), the
   correct linking order (a list of strings), and the public headers of
   the transitive dependencies (another stage).

A target is completely determined by these 3 pieces of data. A consumer
of the target will have no other information available. Hence it is
crucial, that everything (apart from artifacts and runfiles) needed to
build against that target is contained in the provides map.

When the installation of a target is requested on the command line,
artifacts and runfiles are installed; in case of staging conflicts,
artifacts take precedence.

### Source targets

#### Files

If a target is not found in the targets file, it is implicitly
treated as a source file. Both, explicit and implicit source files
look the same. The artifacts stage has a single entry: the path is
the relative path of the file to the module root and the value the
file artifact located at the specified location. The runfiles are
the same as the artifacts and the provides map is empty.

#### (Non-upwards) Symlinks

To ensure self-containedness and location-independence, only
*non-upwards* symlinks are expected and accepted. The symlinks
must not however be necessarily resolvable, i.e., dangling symlinks
are accepted.

An explicit (non-upwards) symlink target is similar to an explicit file target,
except that at the specified location there has to be a non-upwards symlink
rather than a file and the corresponding symlink artifact is taken instead of a
file artifact.

#### Collection of files given by a shell pattern

A collection of files given by a shell pattern has, both as
artifacts and runfiles, the (necessarily disjoint) union of the
artifact maps of the (zero or more) source targets that match the
pattern. Only *files* in the *top-level* directory of the given
modules are considered for matches. The provides map is empty.

#### Trees

A tree describes a directory. Internally, however, it is a single
opaque artifact. Consuming targets cannot look into the internal
structure of that tree. Only when realized in the file system (when
installation is requested or as part of the input to an action), the
directory structure is visible again.

An explicit tree target is similar to an explicit file target,
except that at the specified location there has to be a directory
rather than a file and the tree artifact corresponding to that
directory is taken instead of a file artifact.
