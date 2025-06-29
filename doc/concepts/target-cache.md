Target-level caching
====================

`git` trees as content-fixed roots
----------------------------------

### The `"git tree"` root scheme

The multi-repository configuration supports a scheme `"git tree"`. This
scheme is given by two parameters,

 - the id of the tree (as a string with the hex encoding), and
 - an arbitrary `git` repository containing the specified tree object,
   as well as all needed tree and blob objects reachable from that
   tree.

For example, a root could be specified as follows.

``` jsonc
["git tree", "6a1820e78f61aee6b8f3677f150f4559b6ba77a4", "/usr/local/src/justbuild.git"]
```

It should be noted that the `git` tree identifier alone already
specifies the content of the full tree. However, `just` needs access to
some repository containing the tree in order to know what the tree looks
like.

Nevertheless, it is an important observation that the tree identifier
alone already specifies the content of the whole (logical) directory.
The equality of two such directories can be established by comparing the
two identifiers *without* the need to read any file from
disk. Those "fixed-content" descriptions, i.e., descriptions of a
repository root that already fully determines the content are the key to
caching whole targets.

### `KNOWN` artifacts

The in-memory representation of known artifacts has an optional
reference to a repository containing that artifact. Artifacts "known"
from local repositories might not be known to the CAS used for the
action execution; this additional reference allows to fill such misses
in the CAS.

Content-fixed repositories
--------------------------

### The parts of a content-fixed repository

In order to meaningfully cache a target, we need to be able to
efficiently compute the cache key. We restrict this to the case where we
can compute the information about the repository without file-system
access. This requires that all roots (workspace, target root, etc) be
content fixed, as well as the bindings of the free repository names (and
hence also all transitively reachable repositories). The call such
repositories "content-fixed" repositories.

### Canonical description of a content-fixed repository

The local data of a repository consists of the following.

 - The roots (for workspace, targets, rules, expressions). As the tree
   identifier already defines the content, we leave out the path to the
   repository containing the tree.
 - The names of the targets, rules, and expression files.
 - The names of the outgoing "bindings".

Additionally, repositories can reach additional repositories via
bindings. Moreover, this repository-level dependency relation is not
necessarily cycle free. In particular, we cannot use the tree unfolding
as canonical representation of that graph up to bisimulation, as we do
with most other data structures. To still get a canonical
representation, we factor out the largest bisimulation, i.e., minimize
the respective automaton (with repositories as states, local data as
locally observable properties, and the binding relation as edges).

Finally, for each repository individually, the reachable repositories
are renamed `"0"`, `"1"`, `"2"`, etc, following a depth-first traversal
starting from the repository in question where outgoing edges are
traversed in lexicographical order. The entry point is hence
recognisable as repository `"0"`.

The repository key content-identifier of the canonically formatted
canonical serialisation of the JSON encoding of the obtain
multi-repository configuration (with repository-free git-root
descriptions). The serialisation itself is stored in CAS.

These identifications and replacement of global names does not change
the semantics, as our name data types are completely opaque to our
expression language. In the `"json_encode"` expression, they're
serialized as `null` and string representation is only generated in user
messages not available to the language itself. Moreover, names cannot be
compared for equality either, so their only observable properties, i.e.,
the way `"DEP_ARTIFACTS"`, `"DEP_RUNFILES`, and `"DEP_PROVIDES"` reacts
to them are invariant under repository bisimulation.

Configuration and the `"export"` rule
-------------------------------------

Targets not only depend on the content of their repository, but also on
their configurations. Normally, the effective part of a configuration is
only determined after analysing the target. However, for caching, we
need to compute the cache key directly. This property is provided by the
built-in `"export"` rule; only `"export"` targets residing in
content-fixed repositories will be cached. This also serves as
indication, which targets of a repository are intended for consumption
by other repositories.

An `"export"` rule takes precisely the following arguments.

 - `"target"` specifying a single target, the target to be cached. It
   must not be tainted.
 - `"flexible_config"` a list of strings; those specify the variables
   of the configuration that are considered. All other parts of the
   configuration are ignored. So the effective configuration for the
   `"export"` target is the configuration restricted to those variables
   (filled up with `null` if the variable was not present in the
   original configuration).
 - `"fixed_config"` a dict with of arbitrary JSON values (taken
   unevaluated) with keys disjoint from the `"flexible_config"`.

An `"export"` target is analyzed as follows. The configuration is
restricted to the variables specified in the `"flexible_config"`; this
will result in the effective configuration for the exported target. It
is a requirement that the effective configuration contain only pure JSON
values. The (necessarily conflict-free) union with the `"fixed_config"`
is computed and the `"target"` is evaluated in this configuration. The
result (artifacts, runfiles, provided information) is the result of that
evaluation. It is a requirement that the provided information does only
contain pure JSON values and artifacts (including tree artifacts); in
particular, they may not contain names.

Cache key
---------

We only consider `"export"` targets in content-fixed repositories for
caching. An export target is then fully described by

 - the repository key of the repository the export target resides in,
 - the target name of the export target within that repository,
   described as module-name pair, and
 - the effective configuration.

More precisely, the canonical description is the JSON object with those
values for the keys `"repo_key"`, `"target_name"`, and
`"effective_config"`, respectively. The repository key is the blob
identifier of the canonical serialisation (including sorted keys, etc)
of the just described piece of JSON. To allow debugging and cooperation
with other tools, whenever a cache key is computed, it is ensured, that
the serialisation ends up in the applicable CAS.

It should be noted that the cache key can be computed
*without* analyzing the target referred to. This is
possible, as the configuration is pruned a priori instead of the usual
procedure to analyse and afterwards determine the parts of the
configuration that were relevant.

Cached value
------------

The value to be cached is the result of evaluating the target, that is,
its artifacts, runfiles, and provided data. All artifacts inside those
data structures will be described as known artifacts.

As serialisation, we will essentially use our usual JSON encoding; while
this can be used as is for artifacts and runfiles where we know that
they have to be a map from strings to artifacts, additional information
will be added for the provided data. The provided data can contain
artifacts, but also legitimately pure JSON values that coincide with our
JSON encoding of artifacts; the same holds true for nodes and result
values. Moreover, the tree unfolding implicit in the JSON serialisation
can be exponentially larger than the value.

Therefore, in our serialisation, we add an entry for every subexpression
and separately add a list of which subexpressions are artifacts, nodes,
or results. During deserialisation, we use this subexpression structure
to deserialize every subexpression only once.

Sharding of target cache
------------------------

In our target description, the execution environment is not included.
For local execution, it is implicit anyway. As we also want to cache
high-level targets when using remote execution, we shard the target
cache (e.g., by using appropriate subdirectories) by the blob identifier
of the serialisation of the description of the execution backend. Here,
`null` stands for local execution, and for remote execution we use an
object with keys `"remote_execution_address"` and
`"remote_execution_properties"` filled in the obvious way. As usual, we
add the serialisation to the CAS.

`"export"` targets, strictness and the extensional projection
-------------------------------------------------------------

As opposed to the target that is exported, the corresponding export
target, if part of a content-fixed repository, will be strict: a build
depending on such a target can only succeed if all artifacts in the
result of target (regardless whether direct artifacts, runfiles, or as
part of the provided data) can be built, even if not all (or even none)
are actually used in the build.

Upon cache hit, the artifacts of an export target are the known
artifacts corresponding to the artifacts of the exported target. While
extensionally equal, known artifacts are defined differently, so an
export target and the exported target are intensionally different (and
that difference might only be visible on the second build). As
intensional equality is used when testing for absence of conflicts in
staging, a target and its exported version almost always conflict and
hence should not be used together. One way to achieve this is to always
use the export target for any target that is exported. This fits well
together with the recommendation of only depending on export targets of
other repositories.

If a target forwards artifacts of an exported target (indirect header
files, indirect link dependencies, etc), and is exported again, no
additional conflicts occur; replacing by the corresponding known
artifact is a projection: the known artifact corresponding to a known
artifact is the artifact itself. Moreover, by the strictness property
described earlier, if an export target has a cache hit, then so have all
export targets it depends upon. Keep in mind that a repository can only
be content-fixed if all its dependencies are.

For this strictness-based approach to work, it is, however, a
requirement that any artifact that is exported (typically indirectly,
e.g., as part of a common dependency) by several targets is only used
through the same export target. For a well-structured repository, this
should not be a natural property anyway.

The forwarding of artifacts are the reason we chose that in the
non-cached analysis of an export target the artifacts are passed on as
received and are not wrapped in an "add to cache" action. The latter
choice would violate that projection property we rely upon.

### Example

Consider the following target file (on a content-fixed root) as
example.

``` jsonc
{ "generated":
  {"type": "generic", "outs": ["out.txt"], "cmds": ["echo Hello > out.txt"]}
, "export": {"type": "export", "target": "generated"}
, "use":
  {"type": "install", "dirs": [["generated", "."], ["generated", "other-use"]]}
, "": {"type": "export", "target": "use"}
}
```

Upon initial analysis (on an empty local build root) of the default
target `""`, the output artifact `out.txt` is an action artifact, more
precisely the same one that is output of the target `"generated"`;
the target `"export"` also has the same artifact on output. After
building the default target, a target-cache entry will be written
for this target, containing the extensional definition of the target,
so for `out.txt` the known artifact `e965047ad7c57865...` stored; as
a side effect, also for the target `"export"` a target-cache entry
will be written, containing, of course, the same known artifact.
So on subsequent analysis, both `"export"` and `""` will still
have the same artifact for `out.txt`, but this time a known one.
This artifact is now different from the artifact of the target
`"generated"` (which is still an action artifact), but no conflicts
arise as the usual target discipline requires that any target not
a (direct or indirect) dependency of `"export"` use the target
`"generated"` only indirectly by using the target `"export"`.

Also note that further exporting such a target has to effect, as a
known artifact always evaluates to itself. In that sense, replacing
by the extensional definition is a projection.

### Interaction with garbage collection

While adding the implied export targets happens automatically due
to the evaluation mechanism, the dependencies of target-level cache
entries on one another still have to be persisted to honor them
during garbage collection. Otherwise it would be possible that an
implied target gets garbage collected. In fact, that would even be
likely as typical builds only reference the top-level export targets.


#### Analysis to track the export targets depended upon

As we have to persist this dependency, we need to explicitly track
it. More precisely, the internal data structure of an analyzed
target is extended by a set of all the export targets eligible
for caching, represented by the hashes of the `TargetCacheKey`s,
encountered during the analysis of that target.

### Extension of the value of a target-level cache entry

The cached value for a target-level cache entry is serialized as a
JSON object, with besides the keys `"artifacts"`, `"runfiles"`, and
`"provides"` also a key `"implied export targets"` that lists (in
lexicographic order) the hashes of the cache keys of the export
targets the analysis of the given export target depends upon; the
field is only serialized if that list is non empty.

### Additional invariant honored during uplinking

Our cache honors the additional invariant that, whenever a target-level
cache entry is present, so are the implied target-level cache
entries. This invariant is honored when adding new target-level
cache entries by adding them in the correct order, as well as when
uplinking by uplinking the implied entries first (and there, of
course, honoring the respective invariants).
