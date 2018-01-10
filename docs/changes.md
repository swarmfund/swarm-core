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