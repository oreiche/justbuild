Documentation of build rules, expressions, etc
==============================================

Build rules can obtain a non-trivial complexity. This is especially true
if several rules have to exist for slightly different use cases, or if
the rule supports many different fields. Therefore, documentation of the
rules (and also expressions for the benefit of rule authors) is
desirable.

Experience shows that documentation that is not versioned together with
the code it refers to quickly gets out of date, or lost. Therefore, we
add documentation directly into the respective definitions.

Multi-line strings in JSON
--------------------------

In JSON, the newline character is encoded specially and not taken
literally; also, there is not implicit joining of string literals. So,
in order to also have documentation readable in the JSON representation
itself, instead of single strings, we take arrays of strings, with the
understanding that they describe the strings obtained by joining the
entries with newline characters.

Documentation is optional
-------------------------

While documentation is highly recommended, it still remains optional.
Therefore, when in the following we state that a key is for a list or a
map, it is always implied that it may be absent; in this case, the empty
array or the empty map is taken as default, respectively.

Rules
-----

Each rule is described as a JSON object with a fixed set of keys. So
having fixed keys for documentation does not cause conflicts. More
precisely, the keys `"doc"`, `"field_doc"`, `"config_doc"`, `"artifacts_doc"`,
`"runfiles_doc"`, and `"provides_doc"` are reserved for documentation. Here,
`"doc"` has to be a list of strings describing the rule in general.
`"field_doc"` has to be a map from (some of) the field names to an array
of strings, containing additional information on that particular field.
`"config_doc"` has to be a map from (some of) the config variables to an
array of strings describing the respective variable. `"artifacts_doc"` is
an array of strings describing the artifacts produced by the rule.
`"runfiles_doc"` is an array of strings describing the runfiles produced
by this rule. Finally, `"provides_doc"` is a map describing (some of) the
providers by that rule; as opposed to fields or config variables there
is no authoritative list of providers given elsewhere in the rule, so it
is up to the rule author to give an accurate documentation on the
provided data.

### Example

``` jsonc
{ "library":
  { "doc":
    [ "A C library"
    , ""
    , "Define a library that can be used to be statically linked to a"
    , "binary. To do so, the target can simply be specified in the deps"
    , "field of a binary; it can also be a dependency of another library"
    , "and the information is then propagated to the corresponding binary."
    ]
  , "string_fields": ["name"]
  , "target_fields": ["srcs", "hdrs", "private-hdrs", "deps"]
  , "field_doc":
    { "name":
      ["The base name of the library (i.e., the name without the leading lib)."]
    , "srcs": ["The source files (i.e., *.c files) of the library."]
    , "hdrs":
      [ "The public header files of this library. Targets depending on"
      , "this library will have access to those header files"
      ]
    , "private-hdrs":
      [ "Additional internal header files that are used when compiling"
      , "the source files. Targets depending on this library have no access"
      , "to those header files."
      ]
    , "deps":
      [ "Any other libraries that this library uses. The dependency is"
      , "also propagated (via the link-deps provider) to any consumers of"
      , "this target. So only direct dependencies should be declared."
      ]
    }
  , "config_vars": ["CC"]
  , "config_doc":
    { "CC":
      [ "single string. defaulting to \"cc\", specifying the compiler"
      , "to be used. The compiler is also used to launch the preprocessor."
      ]
    }
  , "artifacts_doc":
    ["The actual library (libname.a) staged in the specified directory"]
  , "runfiles_doc": ["The public headers of this library"]
  , "provides_doc":
    { "compile-deps":
      [ "Map of artifacts specifying any additional files that, besides the runfiles,"
      , "have to be present in compile actions of targets depending on this library"
      ]
    , "link-deps":
      [ "Map of artifacts specifying any additional files that, besides the artifacts,"
      , "have to be present in a link actions of targets depending on this library"
      ]
    , "link-args":
      [ "List of strings that have to be added to the command line for linking actions"
      , "in targets depending on this library"
      ]
    }
  , "expression": { ... }
  }
}
```

Expressions
-----------

Expressions are also described by a JSON object with a fixed set of
keys. Here we use the keys `"doc"` and `"vars_doc"` for documentation, where
`"doc"` is an array of strings describing the expression as a whole and
`"vars_doc"` is a map from (some of) the `"vars"` to an array of strings
describing this variable.

Export targets
--------------

As export targets play the role of interfaces between repositories, it
is important that they be documented as well. Again, export targets are
described as a JSON object with fixed set of keys and we use the keys
`"doc"` and `"config_doc"` for documentation. Here `"doc"` is an array of
strings describing the targeted in general and `"config_doc"` is a map
from (some of) the variables of the `"flexible_config"` to an array of
strings describing this parameter.

Configure targets
-----------------

As configure targets often serve as internal interface to external
export targets (e.g., in order to set a needed configuration), we
support documentation here as well. As configure targets, being
built-in, have a fixed set of fields, a `"doc"` field can be used
for this purpose without conflicts. Again, the `"doc"` field is an
array of strings describing the target in general.

Presentation of the documentation
---------------------------------

As all documentation are just values (that need not be evaluated) in
JSON objects, it is easy to write tool rendering documentation pages for
rules, etc, and we expect those tools to be written independently.
Nevertheless, for the benefit of developers using rules from a git-tree
roots that might not be checked out, there is a subcommand `describe`
which takes a target specification like the `analyze` command, looks up
the corresponding rule and describes it fully, i.e., prints in
human-readable form

 - the documentation for the rule
 - all the fields available for that rule together with
    - their type (`"string_fields"`, `"target_fields"`, etc), and
    - their documentation,
 - all the configuration variables of the rule with their documentation
   (if given), and
 - the documented providers.
