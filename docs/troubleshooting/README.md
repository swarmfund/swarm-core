## Bad hash chain:

Logs example:
```
Catchup COMPLETE verifying history
May 12 12:40:12 ip-10-23-96-132 core[1138]: 2018-05-12T12:40:12.932 GDVCR [History INFO ] Catching up: verifying checkpoint 1/1 (100%)
May 12 12:40:12 ip-10-23-96-132 core[1138]: 2018-05-12T12:40:12.932 GDVCR [History ERROR] Bad hash-chain: [seq=1966525, hash=5730db] wants prev hash 630fe0 but actual prev hash is 3efde6 [HistoryWork.cpp:380]
May 12 12:40:12 ip-10-23-96-132 core[1138]: 2018-05-12T12:40:12.932 GDVCR [History ERROR] Catchup material failed verification, propagating failure [HistoryWork.cpp:498]
May 12 12:40:12 ip-10-23-96-132 core[1138]: 2018-05-12T12:40:12.932 GDVCR [Work WARN ] Scheduling retry #4/5 in 11 sec, for catchup-complete-001e01ad
May 12 12:40:18 ip-10-23-96-132 core[1138]: 2018-05-12T12:40:18.546 GDVCR [Herder INFO ] Quorum information for 1966559 : {"agree":4,"disagree":0,"fail_at":2,"hash":"d8be5a","missing":0,"phase":"EXTERNALIZE"}
May 12 12:40:18 ip-10-23-96-132 core[1138]:
```

1. Findout which block causes problem (TODO: should be included in logs)
Connect to healthy node and try to find ledger with hash:
select * from ledgerheaders where ledgerhash ~ '^(wants_prev_hash).*';

2. Select ledgerheader from unhealthy node by ledgerseq and compare them
select * form ledgerheaders where ledgerseq = <ledger_seq_from_request_1>;

### CASE: bucketlisthash are different
Most probably the issue is due to different results. Find and compare transactions results.