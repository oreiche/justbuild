Multi-repository build
======================

Repository configuration
------------------------

### Open repository names

A repository can have external dependencies. This is realized by having
unbound ("open") repository names being used as references. The actual
definition of those external repositories is not part of the repository;
we think of them as inputs, i.e., we think of this repository as a
function of the referenced external targets.

### Binding in a separate repository configuration

The actual binding of the free repository names is specified in a
separate repository-configuration file, which is specified on the
command line (via the `-C` option); this command-line argument is
optional and the default is that the repository worked on has no
external dependencies. Typically (but not necessarily), this
repository-configuration file is located outside the referenced
repositories and versioned separately or generated from such a file via
`just-mr`. It serves as meta-data for a group of repositories
belonging together.

This file contains one JSON object. For the key `"repositories"` the
value is an object; its keys are the global names of the specified
repositories. For each repository, there is an object describing it. The
key `"workspace_root"` describes where to find the repository and should
be present for all (direct or indirect) external dependencies of the
repository worked upon. Additional roots file names (for target, rule,
and expression) can be specified. For keys not given, the same rules for
default values apply as for the corresponding command-line arguments.
Additionally, for each repository, the key "bindings" specifies the
map of the open repository names to the global names that provide these
dependencies. Repositories may depend on each other (or even
themselves), but the resulting global target graph has to be cycle free.

Whenever a location has to be specified, the value has to be a list,
with the first entry being specifying the naming scheme; the semantics
of the remaining entries depends on the scheme (see "Root Naming
Schemes" below).

Additionally, the optional key `"main"` specifies the main
repository (if omitted, the repository with the lexicographically-first name
is used). The target to be built (as specified on the command line) is
taken from this repository. Also, the command-line arguments `-w`,
`--target_root`, etc, apply to this repository. If no option `-w` is
given and `"workspace_root"` is not specified in the
repository-configuration file either, the root is determined from the
working directory as usual.

The value of `main` can be overwritten on the command line (with the
`--main` option) In this way, a consistent configuration of
interdependent repositories can be versioned and referred to regardless
of the repository worked on.

#### Root naming scheme

##### `"file"`

The `"file"` scheme tells that the repository (or respective
root) can be found in a directory in the local file system; the
only argument is the absolute path to that directory.

##### `"git tree"`

The `"git tree"` scheme tells that the root is defined to be a
tree given by a git tree identifier. It takes two arguments

 - the tree identifier, as hex-encoded string, and
 - the absolute path to some repository containing that tree

#### Example

Consider, for example, the following repository-configuration file.
In the following, we assume it is located at `/etc/just/repos.json`.

``` jsonc
{ "main": "env"
, "repositories":
  { "foobar":
    { "workspace_root": ["file", "/opt/foobar/repo"]
    , "rule_root": ["file", "/etc/just/rules"]
    , "bindings": {"base": "barimpl"}
    }
  , "barimpl":
    { "workspace_root": ["file", "/opt/barimpl"]
    , "target_file_name": "TARGETS.bar"
    }
  , "env": {"bindings": {"foo": "foobar", "bar": "barimpl"}}
  }
}
```

It specifies 3 repositories, with global names `foobar`, `barimpl`,
and `env`. Within `foobar`, the repository name `base` refers to
`barimpl`, the repository that can be found at `/opt/barimpl`.

The repository `env` is the main repository and there is no
workspace root defined for it, so it only provides bindings for
external repositories `foo` and `bar`, but the actual repository is
taken from the working directory (unless `-w` is specified). In this
way, it provides an environment for developing applications based on
`foo` and `bar`.

For example, the invocation `just build -C /etc/just/repos.conf
baz` tells our tool to build the target `baz` from the module the
working directory is located in. `foo` will refer to the repository
found at `/opt/foobar/repo` (using rules from `/etc/just/rules`,
taking `base` refer to the repository at `/opt/barimpl`) and `bar`
will refer to the repository at `/opts/barimpl`.

Naming of targets
-----------------

### Reference in target files

In addition to the normal target references (string for a target in the
name module, module-target pair for a target in same repository,
`["./", relpath, target]` relative addressing, `["FILE", null,
name]` explicit file reference in the same module), references of the
form `["@", repo, module, target]` can be specified, where `repo` is
string referring to an open name. That open repository name is resolved
to the global name by the `"bindings"` parameter of the repository the
target reference is made in. Within the repository the resolved name
refers to, the target `[module, target]` is taken.

### Expression language: names as abstract values

Targets are a global concept as they distinguish targets from different
repositories. Their names, however, depend on the repository they occur
in (as the local names might differ in various repositories). Moreover,
some targets cannot be named in certain repositories as not every
repository has a local name in every other repository.

To handle this naming problem, we note the following. During the
evaluation of a target names occur at two places: as the result of
evaluating the parameters (for target fields) and in the evaluation of
the defining expression when requesting properties of a target dependent
upon (via `DEP_ARTIFACTS` and related functions). In the later case,
however, the only legitimate way to obtain a target name is by the
`FIELD` function. To enforce this behavior, and to avoid problems with
serializing target names, our expression language considers target names
as opaque values. More precisely,

 - in a target description, the target fields are evaluated and the
   result of the evaluation is parsed, in the context of the module the
   `TARGET` file belongs to, as a target name, and
 - during evaluation of the defining expression of a the target's
   rule, when accessing `FIELD` the values of target fields will be
   reported as abstract name values and when querying values of
   dependencies (via `DEP_ARTIFACTS` etc) the correct abstract target
   name has to be provided.

While the defining expression has access to target names (via target
fields), it is not useful to provide them in provided data; a consuming
data cannot use names unless it has those fields as dependency anyway.
Our tool will not enforce this policy; however, only targets not having
names in their provided data are eligible to be used in `export` rules.

File layout in actions
----------------------

As `just` does full staging for actions, no special considerations are
needed when combining targets of different repositories. Each target
brings its staging of artifacts as usual. In particular, no repository
names (neither local nor global ones) will ever be visible in any
action. So for the consuming target it makes no difference if its
dependency comes from the same or a different repository.
