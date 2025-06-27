User-defined Rules
==================

Targets are defined in terms of high-level concepts like "libraries",
"binaries", etc. In order to translate these high-level definitions
into actionable tasks, the user defines rules, explaining at a single
point how all targets of a given type are built.

Rules files
-----------

Rules are defined in rules files (by default named `RULES`). Those
contain a JSON object mapping rule names to their rule definition. For
rules, the same naming scheme as for targets applies. However, built-in
rules (always named by a single string) take precedence in naming; to
explicitly refer to a rule defined in the current module, the module has
to be specified, possibly by a relative path, e.g.,
`["./", ".", "install"]`.

Basic components of a rule
--------------------------

A rule is defined through a JSON object with various keys. The only
mandatory key is `"expression"` containing the defining expression of
the rule.

### `"config_fields"`, `"string_fields"` and `"target_fields"`

These keys specify the fields that a target defined by that rule can
have. In particular, those have to be disjoint lists of strings.

For `"config_fields"` and `"string_fields"` the respective field has to
evaluate to a list of strings, whereas `"target_fields"` have to
evaluate to a list of target references. Those references are evaluated
immediately, and in the name context of the target they occur in.

The difference between `"config_fields"` and `"string_fields"` is that
`"config_fields"` are evaluated before the target fields and hence can
be used by the rule to specify config transitions for the target fields.
`"string_fields"` on the other hand are evaluated *after*
the target fields; hence the rule cannot use them to specify a
configuration transition, however the target definition in those fields
may use the `"outs"` and `"runfiles"` functions to have access to the
names of the artifacts or runfiles of a target specified in one of the
target fields.

### `"implicit"`

This key specifies a map of implicit dependencies. The keys of the map
are additional target fields, the values are the fixed list of targets
for those fields. If a short-form name of a target is used (e.g., only a
string instead of a module-target pair), it is interpreted relative to
the repository and module the rule is defined in, not the one the rule
is used in. Other than this, those fields are evaluated the same way as
target fields settable on invocation of the rule.

### `"config_vars"`

This is a list of strings specifying which parts of the configuration
the rule uses. The defining expression of the rule is evaluated in an
environment that is the configuration restricted to those variables; if
one of those variables is not specified in the configuration the value
in the restriction is `null`.

### `"config_transitions"`

This key specifies a map of (some of) the target fields (whether
declared as `"target_fields"` or as `"implicit"`) to a configuration
expression. Here, a configuration expression is any expression in our
language. It has access to the `"config_vars"` and the `"config_fields"`
and has to evaluate to a list of maps. Each map specifies a transition
to the current configuration by amending it on the domain of that map to
the given value.

### `"imports"`

This specifies a map of expressions that can later be used by
`CALL_EXPRESSION`. In this way, duplication of (rule) code can be
avoided. For each key, we have to have a name of an expression;
expressions are named following the same naming scheme as targets and
rules. The names are resolved in the context of the rule. Expressions
themselves are defined in expression files, the default name being
`EXPRESSIONS`.

Each expression is a JSON object. The only mandatory key is
`"expression"` which has to be an expression in our language. It
optionally can have a key `"vars"` where the value has to be a list of
strings (and the default is the empty list). Additionally, it can have
another optional key `"imports"` following the same scheme as the
`"imports"` key of a rule; in the `"imports"` key of an expression,
names are resolved in the context of that expression. It is a
requirement that the `"imports"` graph be cycle free.

### `"expression"`

This specifies the defining expression of the rule. The value has to be
an expression of our expression language (basically, an abstract syntax
tree serialized as JSON). It has access to the following extra functions
and, when evaluated, has to return a result value.

#### `FIELD`

The field function takes one argument, `name` which has to evaluate
to the name of a field. For string fields, the given list of strings
is returned; for target fields, the list of abstract names for the
given target is returned. These abstract names are opaque within the
rule language (but meaningful when reported in error messages) and
should only be used to be passed on to other functions that expect
names as inputs.

#### `DEP_ARTIFACTS` and `DEP_RUNFILES`

These functions give access to the artifacts, or runfiles,
respectively, of one of the targets depended upon. It takes two
(evaluated) arguments, the mandatory `"dep"` and the optional
`"transition"`.

The argument `"dep"` has to evaluate to an abstract name (as can be
obtained from the `FIELD` function) of some target specified in one
of the target fields. The `"transition"` argument has to evaluate to
a configuration transition (i.e., a map) and the empty transition is
taken as default. It is an error to request a target-transition pair
for a target that was not requested in the given transition through
one of the target fields.

#### `DEP_PROVIDES`

This function gives access to a particular entry of the provides map
of one of the targets depended upon. The arguments `"dep"` and
`"transition"` are as for `DEP_ARTIFACTS`; additionally, there is
the mandatory argument `"provider"` which has to evaluate to a
string. The function returns the value of the provides map of the
target at the given provider. If the key is not in the provides map
(or the value at that key is `null`), the optional argument
`"default"` is evaluated and returned. The default for `"default"`
is the empty list.

#### `BLOB`

The `BLOB` function takes a single (evaluated) argument `data` which
is optional and defaults to the empty string. This argument has to
evaluate to a string. The function returns an artifact that is a
non-executable file with the given string as content.

#### `TREE`

The `TREE` function takes a single (evaluated) argument `$1` which
has to be a map of artifacts. The result is a single tree artifact
formed from the input map. It is an error if the map cannot be
transformed into a tree (e.g., due to staging conflicts).

### `TREE_OVERLAY` and `DISJOINT_TREE_OVERLAY`

The `TREE_OVERLAY` and `DISJOINT_TREE_OVERLAY` functions take a
single (evaluated) argument `$1` which has to be a list of maps
of artifacts. Each entry in the list is transformed into a single
tree artifact formed from that map and it is an error if the map
cannot be transformed into a tree (e.g., due to a staging conflict).
The result is a single tree artifact defined as the overlay of the
specified trees in that order (latest wins on each path after build
evaluation); in the case of `DISJOINT_TREE_OVERLAY` it is a build
error if a non-trivial conflict arises when computing the overlay
in the build phase.

#### `ACTION`

Actions are a way to define new artifacts from (zero or more)
already defined artifacts by running a command, typically a
compiler, linker, archiver, etc. The action function takes the
following arguments.

 - `"inputs"` A map of artifacts. These artifacts are present when
   the command is executed; the keys of the map are the relative
   path from the working directory of the command. The command must
   not make any assumption about the location of the working
   directory in the file system (and instead should refer to files
   by path relative to the working directory). Moreover, the
   command must not modify the input files in any way. (In-place
   operations can be simulated by staging, as is shown in the
   example later in this document.)

   It is an additional requirement that no conflicts occur when
   interpreting the keys as paths. For example, `"foo.txt"` and
   `"./foo.txt"` are different as strings and hence legitimately
   can be assigned different values in a map. When interpreted as a
   path, however, they name the same path; so, if the `"inputs"`
   map contains both those keys, the corresponding values have to
   be equal.

 - `"cmd"` The command to execute, given as `argv` vector, i.e., a
   non-empty list of strings. The 0'th element of that list will
   also be the program to be executed.

 - `"cwd"` The directory inside the action root to change to before
   executing the command. The directory has to be given as a string
   describing a non-upwards relative path. This field is optional
   and defaults to `""`.

 - `"env"` The environment in which the command should be executed,
   given as a map of strings to strings.

 - `"outs"` and `"out_dirs"` Two list of strings naming the files
   and directories, respectively, the command is expected to
   create. Those paths are interpreted relative to the action root
   (not relative to `"cwd"`).
   It is an error if the command fails to create the
   promised output files. These two lists have to be disjoint, but
   an entry of `"outs"` may well name a location inside one of the
   `"out_dirs"`.

This function returns a map with keys the strings mentioned in
`"outs"` and `"out_dirs"`. As values this map has artifacts defined
to be the ones created by running the given command (in the given
environment with the given inputs).

#### `RESULT`

The `RESULT` function is the only way to obtain a result value. It
takes three (evaluated) arguments, `"artifacts"`, `"runfiles"`, and
`"provides"`, all of which are optional and default to the empty
map. It defines the result of a target that has the given artifacts,
runfiles, and provided data, respectively. In particular,
`"artifacts"` and `"runfiles"` have to be maps to artifacts, and
`"provides"` has to be a map. Moreover, the keys in `"runfiles"`
and `"artifacts"` are treated as paths; it is an error if this
interpretation yields to conflicts. The keys in the artifacts or
runfile maps as seen by other targets are the normalized paths of
the keys given.

Result values themselves are opaque in our expression language and
cannot be deconstructed in any way. Their only purpose is to be the
result of the evaluation of the defining expression of a target.

#### `CALL_EXPRESSION`

This function takes one mandatory argument `"name"` which is
unevaluated; it has to a be a string literal. The expression
imported by that name through the imports field is evaluated in the
current environment restricted to the variables of that expression.
The result of that evaluation is the result of the `CALL_EXPRESSION`
statement.

During the evaluation of an expression, rule fields can still be
accessed through the functions `FIELD`, `DEP_ARTIFACTS`, etc. In
particular, even an expression with no variables (that, hence, is
always evaluated in the empty environment) can carry out non-trivial
computations and be non-constant. The special functions `BLOB`,
`ACTION`, and `RESULT` are also available. If inside the evaluation
of an expression the function `CALL_EXPRESSION` is used, the name
argument refers to the `"imports"` map of that expression. So the
call graph is deliberately recursion free.

Evaluation of a target
----------------------

A target defined by a user-defined rule is evaluated in the following
way.

 - First, the config fields are evaluated.

 - Then, the target-fields are evaluated. This happens for each field
   as follows.

    - The configuration transition for this field is evaluated and the
      transitioned configurations determined.
    - The argument expression for this field is evaluated. The result
      is interpreted as a list of target names. Each of those targets
      is analyzed in all the specified configurations.

 - The string fields are evaluated. If the expression for a string
   field queries a target (via `outs` or `runfiles`), the value for
   that target is returned in the first configuration. The rational
   here is that such generator expressions are intended to refer to the
   corresponding target in its "main" configuration; they are hardly
   used anyway for fields branching their targets over many
   configurations.

 - The effective configuration for the target is determined. The target
   effectively has used of the configuration the variables used by the
   `arguments_config` in the rule invocation, the `config_vars` the
   rule specified, and the parts of the configuration used by a target
   dependent upon. For a target dependent upon, all parts it used of
   its configuration are relevant expect for those fixed by the
   configuration transition.

 - The rule expression is evaluated and the result of that evaluation
   is the result of the rule.

Example of developing a rule
----------------------------

Let's consider step by step an example of writing a rule. Say we want
to write a rule that programmatically patches some files.

### Framework: The minimal rule

Every rule has to have a defining expression evaluating to a `RESULT`.
So the minimally correct rule is the `"null"` rule in the following
example rule file.

    { "null": {"expression": {"type": "RESULT"}}}

This rule accepts no parameters, and has the empty map as artifacts,
runfiles, and provided data. So it is not very useful.

### String inputs

Let's allow the target definition to have some fields. The most simple
fields are `string_fields`; they are given by a list of strings. In the
defining expression we can access them directly via the `FIELD`
function. Strings can be used when defining maps, but we can also create
artifacts from them, using the `BLOB` function. To create a map, we can
use the `singleton_map` function. We define values step by step, using
the `let*` construct.

``` jsonc
{ "script only":
  { "string_fields": ["script"]
  , "expression":
    { "type": "let*"
    , "bindings":
      [ [ "script content"
        , { "type": "join"
          , "separator": "\n"
          , "$1":
            { "type": "++"
            , "$1":
              [["H"], {"type": "FIELD", "name": "script"}, ["w", "q", ""]]
            }
          }
        ]
      , [ "script"
        , { "type": "singleton_map"
          , "key": "script.ed"
          , "value":
            {"type": "BLOB", "data": {"type": "var", "name": "script content"}}
          }
        ]
      ]
    , "body":
      {"type": "RESULT", "artifacts": {"type": "var", "name": "script"}}
    }
  }
}
```

### Target inputs and derived artifacts

Now it is time to add the input files. Source files are targets like any
other target (and happen to contain precisely one artifact). So we add a
target field `"srcs"` for the file to be patched. Here we have to keep
in mind that, on the one hand, target fields accept a list of targets
and, on the other hand, the artifacts of a target are a whole map. We
chose to patch all the artifacts of all given `"srcs"` targets. We can
iterate over lists with `foreach` and maps with `foreach_map`.

Next, we have to keep in mind that targets may place their artifacts at
arbitrary logical locations. For us that means that first we have to
make a decision at which logical locations we want to place the output
artifacts. As one thinks of patching as an in-place operation, we chose
to logically place the outputs where the inputs have been. Of course, we
do not modify the input files in any way; after all, we have to define a
mathematical function computing the output artifacts, not a collection
of side effects. With that choice of logical artifact placement, we have
to decide what to do if two (or more) input targets place their
artifacts at logically the same location. We could simply take a
"latest wins" semantics (keep in mind that target fields give a list
of targets, not a set) as provided by the `map_union` function. We chose
to consider it a user error if targets with conflicting artifacts are
specified. This is provided by the `disjoint_map_union` that also allows
to specify an error message to be provided the user. Here, conflict
means that values for the same map position are defined in a different
way.

The actual patching is done by an `ACTION`. We have the script already;
to make things easy, we stage the input to a fixed place and also expect
a fixed output location. Then the actual command is a simple shell
script. The only thing we have to keep in mind is that we want useful
output precisely if the action fails. Also note that, while we define
our actions sequentially, they will be executed in parallel, as none of
them depends on the output of another one of them.

``` jsonc
{ "ed patch":
  { "string_fields": ["script"]
  , "target_fields": ["srcs"]
  , "expression":
    { "type": "let*"
    , "bindings":
      [ [ "script content"
        , { "type": "join"
          , "separator": "\n"
          , "$1":
            { "type": "++"
            , "$1":
              [["H"], {"type": "FIELD", "name": "script"}, ["w", "q", ""]]
            }
          }
        ]
      , [ "script"
        , { "type": "singleton_map"
          , "key": "script.ed"
          , "value":
            {"type": "BLOB", "data": {"type": "var", "name": "script content"}}
          }
        ]
      , [ "patched files per target"
        , { "type": "foreach"
          , "var": "src"
          , "range": {"type": "FIELD", "name": "srcs"}
          , "body":
            { "type": "foreach_map"
            , "var_key": "file_name"
            , "var_val": "file"
            , "range":
              {"type": "DEP_ARTIFACTS", "dep": {"type": "var", "name": "src"}}
            , "body":
              { "type": "let*"
              , "bindings":
                [ [ "action output"
                  , { "type": "ACTION"
                    , "inputs":
                      { "type": "map_union"
                      , "$1":
                        [ {"type": "var", "name": "script"}
                        , { "type": "singleton_map"
                          , "key": "in"
                          , "value": {"type": "var", "name": "file"}
                          }
                        ]
                      }
                    , "cmd":
                      [ "/bin/sh"
                      , "-c"
                      , "cp in out && chmod 644 out && /bin/ed out < script.ed > log 2>&1 || (cat log && exit 1)"
                      ]
                    , "outs": ["out"]
                    }
                  ]
                ]
              , "body":
                { "type": "singleton_map"
                , "key": {"type": "var", "name": "file_name"}
                , "value":
                  { "type": "lookup"
                  , "map": {"type": "var", "name": "action output"}
                  , "key": "out"
                  }
                }
              }
            }
          }
        ]
      , [ "artifacts"
        , { "type": "disjoint_map_union"
          , "msg": "srcs artifacts must not overlap"
          , "$1":
            { "type": "++"
            , "$1": {"type": "var", "name": "patched files per target"}
            }
          }
        ]
      ]
    , "body":
      {"type": "RESULT", "artifacts": {"type": "var", "name": "artifacts"}}
    }
  }
}
```

A typical invocation of that rule would be a target file like the
following.

``` jsonc
{ "input.txt":
  { "type": "ed patch"
  , "script": ["%g/world/s//user/g", "%g/World/s//USER/g"]
  , "srcs": [["FILE", null, "input.txt"]]
  }
}
```

As the input file has the same name as a target (in the same module), we
use the explicit file reference in the specification of the sources.

### Implicit dependencies and config transitions

Say, instead of patching a file, we want to generate source files from
some high-level description using our actively developed code generator.
Then we have to do some additional considerations.

 - First of all, every target defined by this rule not only depends on
   the targets the user specifies. Additionally, our code generator is
   also an implicit dependency. And as it is under active development,
   we certainly do not want it to be taken from the ambient build
   environment (as we did in the previous example with `ed` which,
   however, is a pretty stable tool). So we use an `implicit` target
   for this.
 - Next, we notice that our code generator is used during the build. In
   particular, we want that tool (written in some compiled language) to
   be built for the platform we run our actions on, not the target
   platform we build our final binaries for. Therefore, we have to use
   a configuration transition.
 - As our defining expression also needs the configuration transition
   to access the artifacts of that implicit target, we better define it
   as a reusable expression. Other rules in our rule collection might
   also have the same task; so `["transitions", "for host"]` might be a
   good place to define it. In fact, it can look like the expression
   with that name in our own code base.

So, the overall organization of our rule might be as follows.

``` jsonc
{ "generated code":
  { "target_fields": ["srcs"]
  , "implicit": {"generator": [["generators", "foogen"]]}
  , "config_vars": ["HOST_ARCH"]
  , "imports": {"for host": ["transitions", "for host"]}
  , "config_transitions":
    {"generator": [{"type": "CALL_EXPRESSION", "name": "for host"}]}
  , "expression": ...
  }
}
```

### Providing information to consuming targets

In the simple case of patching, the resulting file is indeed the only
information the consumer of that target needs; in fact, the main point
was that the resulting target could be a drop-in replacement of a source
file. A typical rule, however, defines something like a library and a
library is much more, than just the actual library file and the public
headers: a library may depend on other libraries; therefore, in order to
use it, we need

 - to have the header files of dependencies available that might be
   included by the public header files of that library,
 - to have the libraries transitively depended upon available during
   linking, and
 - to know the order in which to link the dependencies (as they might
   have dependencies among each other).

In order to keep a maintainable build description, all this should be
taken care of by simply depending on that library. We do
*not* want the consumer of a target having to be aware of
such transitive dependencies (e.g., when constructing the link command
line), as it used to be the case in early build tools like `make`.

It is a deliberate design choice that a target is given only by the
result of its analysis, regardless of where it is coming from.
Therefore, all this information needs to be part of the result of a
target. Such kind of information is precisely, what the mentioned
`"provides"` map is for. As a map, it can contain an arbitrary amount of
information and the interface function `"DEP_PROVIDES"` is in such a way
that adding more providers does not affect targets not aware of them
(there is no function asking for all providers of a target). The keys
and their meaning have to be agreed upon by a target and its consumers.
As the latter, however, typically are a target of the same family
(authored by the same group), this usually is not a problem.

A typical example of computing a provided value is the `"link-args"` in
the rules used by `just` itself. They are defined by the following
expression.

``` jsonc
{ "type": "nub_right"
, "$1":
  { "type": "++"
  , "$1":
    [ {"type": "keys", "$1": {"type": "var", "name": "lib"}}
    , {"type": "CALL_EXPRESSION", "name": "link-args-deps"}
    , {"type": "var", "name": "link external", "default": []}
    ]
  }
}
```

This expression

 - collects the respective provider of its dependencies,
 - adds itself in front, and
 - deduplicates the resulting list, keeping only the right-most
   occurrence of each entry.

In this way, the invariant is kept, that the `"link-args"` from a
topological ordering of the dependencies (in the order that a each entry
is mentioned before its dependencies).
