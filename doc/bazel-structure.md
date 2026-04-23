Tests are directly linked in the components.
We don't support building a component without its tests;
the tests are fairly small.

The only exception to that are modules that //src/tests itself depends on.
Those modules must have their tests in separate targets
to avoid circular dependencies.
When that happens, a `cc_library` entry must be defined
for the tests in each module.
For convenience, the top-level directory must contain a `cc_library`
with name `tests` that depends on all such modules.
