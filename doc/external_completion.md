# External Completion

## Introduction

External completion is an Edge feature where Edge runs an external binary
providing information about its state.
The external binary outputs Edge VM code that Edge will interprete.
This code should produce a `TransformationOutput` value,
which Edge will apply to the current buffer.

The goal of this feature is to make it easy to integrate Edge
with AI-assisted workflows.

## External completion command

The buffer variable `external_completion_command` is used
to configure the binary to execute.
