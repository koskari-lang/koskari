# Release and versioning

This page describes the user-facing release story for Koskari.

## Version source

Published packages should expose their version through the package metadata and
runtime package version. When you are working from a checkout, prefer the project
build and test instructions before treating the checkout as releasable.

## Release notes

User-visible changes belong in the [changelog](../changelog.md). Good release
notes answer three questions:

1. What changed?
2. Who is affected?
3. What should a user do differently?

## Compatibility notes

Koskari is both a language and a Python package. A release can affect syntax,
runtime behavior, Python interop, command-line behavior, or documentation. Call
out compatibility in the area where users will notice it.

## Before publishing

A release should have:

- passing project tests,
- generated documentation that builds without warnings from changed pages,
- examples that match the current implementation,
- a changelog entry for user-visible changes.
