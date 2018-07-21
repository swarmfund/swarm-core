#include "ContractHelper.h"

using namespace std;
using namespace soci;

namespace stellar
{
    const char* contractSelector = "";

    void ContractHelper::dropAll(Database &db)
    {
        db.getSession() << "DROP TABLE IF EXISTS contracts;";
        db.getSession() << "CREATE TABLE contracts"
                           "("
                           "id              BIGINT      NOT NULL CHECK (id >= 0) PRIMARY KEY,"
                           "contractor      VARCHAR(56) NOT NULL,"
                           "customer        VARCHAR(56) NOT NULL,"
                           "judge           VARCHAR(56) NOT NULL,"
                           "invoices        TEXT        NOT NULL,"
                           "start_time      BIGINT      NOT NULL,"
                           "end_time        BIGINT      NOT NULL,"
                           "details         TEXT        NOT NULL,"
                           "lastmodified    INT         NOT NULL,"
                           "version         INT         NOT NULL"
                           ");";
    }

    void
    ContractHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry)
    {
        auto contractFrame = make_shared<ContractFrame>(entry);
        auto contractEntry = contractFrame->getContract();

        contractFrame->touch(delta);

        auto key = contractFrame->getKey();
        flushCachedEntry(key, db);

        string sql;

        if (insert)
        {
            sql = "INSERT INTO contracts (id, contractor, customer, judge, invoices, start_time, end_time, "
                  "                       details, lastmodified, version) "
                  "VALUES (:id, :contr, :cust, :jud, :inv, :s_t, :e_t, :det, :lm, :v)";
        }
        else
        {
            sql = "UPDATE contracts "
                  "SET    contractor = :contr, customer = :cust, judge = :jud, invoices = :inv,"
                  "       start_time = :s_t, end_time = :e_t, details = :det,"
                  "       lastmodified = :lm, version = :v "
                  "WHERE  id = :id";
        }
    }
}