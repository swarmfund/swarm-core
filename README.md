# Core

Core is a replicated state machine that maintains a local copy of a cryptographic ledger and processes transactions against it, in consensus with a set of peers (forked from [stellar-core](https://github.com/stellar/stellar-core)).
It implements the [Stellar Consensus Protocol](https://github.com/stellar/stellar-core/blob/master/src/scp/readme.md), a _federated_ consensus protocol.
It is written in C++11 and runs on Linux, OSX and Windows.
Learn more by reading the [overview document](https://github.com/swarmfund/swarm-core/blob/master/docs/readme.md).

# Documentation

Documentation of the code's layout and abstractions, as well as for the
functionality available, can be found in
[`./docs`](https://github.com/swarmfund/swarm-core/tree/master/docs).

# Builiding 

## On newer distributions with OpenSSL 1.1:

```sh
git submodule update --init
cmake CMakeLists.txt -DPostgreSQL_INCLUDE_DIRS=/usr/include/postgresql/ -DPostgreSQL_LIBRARIES=/usr/lib/libpq.so -DOPENSSL_ROOT_DIR=/usr/lib/openssl-1.0 -DOPENSSL_LIBRARIES=/usr/lib/openssl-1.0 -DOPENSSL_INCLUDE_DIR=/usr/include/openssl-1.0
make -j8
```

Expect warning:

```
warning: libcrypto.so.1.1, needed by /usr/lib/libpq.so, may conflict with libcrypto.so.1.0.0
```

Make sure You typed correct paths to the libs. Consider replacing `-j8` according to the number outputed by:

```sh
# nproc
```

## On older distributions with OpenSSL 1.0:

```sh
git submodule update --init
cmake CMakeLists.txt -DPostgreSQL_INCLUDE_DIRS=/usr/include/postgresql/ -DPostgreSQL_LIBRARIES=/usr/lib/libpq.so
make -j8
```

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

# Swarm vs stellar-core
The core of **swarmfund** is based on the open-source project [stellar-core](github.com/stellar/stellar-core).
Swarm uses SCP as consensus protocol, concept of ledger
and have the data flow process as stellar-core.
However there are several important improvements.    

### Account and signer types
Basically, accounts in **stellar-core** all have equal 
possibilities for performing any kind of operation. 
**Swarmfund**'s core introduces account types (e.g. master account, general account etc).
Any operation falls restrictions about account types which 
could perform it. As an example, this change makes it possible to create very easily 
certain system operations which should not be performed by ordinary accounts. 
Swarm going farther and introduces signer types.
Both together this feature contributes to flexibility of how operations are performed.
    
### Issuance
One of the most noticeable change is the process of issuance of new assets.
In order to issue some amount of a new asset in **stellar-core**, the receiver must 
hold a trust line in corresponding asset. The amount can be transferred to receiver via 
payment operation.
**Swarmfund** uses more sophisticated approach and divides this process into  
processing of pre-issuance and issuance requests. Durring asset creation (which is two step process for syndicates),
syndicate is able to provide information like max issuance amount and pre-issuance signer, which restrics total amount of that asset availalbe in the system.
After asset request is approved, syndicate is able to send pre-issuance request (which includes signature of pre-issuance signer).
As soon as this request is approve by the admin of the system (signer of master account), syndicate is able to send issuance request which must include unique reference of such request
(if pre-issued amount is not sufficient, pending issuance request will be created
which can be approved by the syndicate, when sufficient pre-issued amount is availalbe or reject it).
Such an approach is much more safe and reliable and makes integrations with **Swarmfund** much easier. 

### Flexible fee
**Stellar-core** have only a constant fee per transaction.
In **swarmfund** it was implemented a reach system of fee charging which 
gives and opportunity to set different fees for different operations (by introducing fee type),
set fees for each account type or even for certain account.
Also it is possible to charge a percent fee from an amount.

### New operations
**Swarm** contains a big list of operations 
comparing to **stellar-core** which is enough to fulfill needs encountered by tokens management systems.
