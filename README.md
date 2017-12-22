# Core

Core is a replicated state machine that maintains a local copy of a cryptographic ledger and processes transactions against it, in consensus with a set of peers (forked from [stellar-core](https://github.com/stellar/stellar-core)).
It implements the [Stellar Consensus Protocol](https://github.com/stellar/stellar-core/blob/master/src/scp/readme.md), a _federated_ consensus protocol.
It is written in C++11 and runs on Linux, OSX and Windows.
Learn more by reading the [overview document](https://github.com/swarmfund/swarm-core/blob/master/docs/readme.md).

# Documentation

Documentation of the code's layout and abstractions, as well as for the
functionality available, can be found in
[`./docs`](https://github.com/swarmfund/swarm-core/tree/master/docs).

# Installation

See [Installation](./INSTALL.md) Note: to build core you will need to replace submodule https://gitlab.com/swarmfund/xdr with https://github.com/swarmfund/swarm-xdr. 

# Contributing

See [Contributing](./CONTRIBUTING.md)

# Reporting issues

Software has bugs, or maybe you have an idea for a change in core.

Checklist
 1. do a search of issues in case there is one already tracking the one you ran into.
 2. search open issues (not addressed yet) using the filter `is:open` (default). If you have new information, include it into the issue.
 3. search closed issues by removing the `is:open` filter. Two possibilities here:
     * the issue was resolved in a newer version - then you just need to install the version with the fix
     * the issue was closed for some reason. You may decide to reopen it depending on context. Make sure to explain why the issue should be re-opened.

For bugs being opened/re-opened, simply paste and fill the [Bug-Template.md](./Bug-Template.md) into the issue.

# Running tests

run tests with:
  `src/stellar-core --test` (for now only [tx] tests are availalbe)

run one test with:
  `src/stellar-core --test  testName`

run one test category with:
  `src/stellar-core --test '[categoryName]'`

Categories (or tags) can be combined: AND-ed (by juxtaposition) or OR-ed (by comma-listing).

Tests tagged as [.] or [hide] are not part of the default test test.

supported test options can be seen with
  `src/stellar-core --test --help`

display tests timing information:
  `src/stellar-core --test -d yes '[categoryName]'`

xml test output (includes nested section information):
  `src/stellar-core --test -r xml '[categoryName]'`