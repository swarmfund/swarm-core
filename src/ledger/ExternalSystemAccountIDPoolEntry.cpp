#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "ledger/ExternalSystemAccountIDPoolEntry.h"
#include "lib/util/format.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

    ExternalSystemAccountIDPoolEntryFrame::
    ExternalSystemAccountIDPoolEntryFrame() : EntryFrame(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID_POOL_ENTRY),
                                             mExternalSystemAccountIDPoolEntry(mEntry.data.
                                                     externalSystemAccountIDPoolEntry())
    {
    }

    ExternalSystemAccountIDPoolEntryFrame::
    ExternalSystemAccountIDPoolEntryFrame(LedgerEntry const &from)
            : EntryFrame(from), mExternalSystemAccountIDPoolEntry(mEntry.data.externalSystemAccountIDPoolEntry())
    {
    }

    ExternalSystemAccountIDPoolEntryFrame::ExternalSystemAccountIDPoolEntryFrame(
            ExternalSystemAccountIDPoolEntryFrame const &from)
            : ExternalSystemAccountIDPoolEntryFrame(from.mEntry)
    {
    }

    ExternalSystemAccountIDPoolEntryFrame&
    ExternalSystemAccountIDPoolEntryFrame::operator=(ExternalSystemAccountIDPoolEntryFrame const &other)
    {
        if (&other != this)
        {
            mExternalSystemAccountIDPoolEntry = other.mExternalSystemAccountIDPoolEntry;
            mKey = other.mKey;
            mKeyCalculated = other.mKeyCalculated;
        }
        return *this;
    }

    ExternalSystemAccountIDPoolEntryFrame::pointer
    ExternalSystemAccountIDPoolEntryFrame::createNew(uint64 poolEntryID, ExternalSystemType const externalSystemType,
                                                    std::string const data)
    {
        LedgerEntry le;
        le.data.type(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID_POOL_ENTRY);
        auto& externalSystemAccountIDPoolEntry = le.data.externalSystemAccountIDPoolEntry();
        externalSystemAccountIDPoolEntry.poolEntryID = poolEntryID;
        externalSystemAccountIDPoolEntry.externalSystemType = externalSystemType;
        externalSystemAccountIDPoolEntry.data = data;
        return std::make_shared<ExternalSystemAccountIDPoolEntryFrame>(le);
    }

    void ExternalSystemAccountIDPoolEntryFrame::ensureValid(ExternalSystemAccountIDPoolEntry const& p)
    {
        try
        {
            if (!isValidEnumValue(p.externalSystemType))
            {
                throw runtime_error("Invalid enum value of external system type");
            }

            if (p.data.empty())
            {
                throw runtime_error("Data cannot be empty string");
            }
        }
        catch (...)
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state, external system account id pool entry is invalid: "
                                               << xdr::xdr_to_string(p);
            throw_with_nested(runtime_error("External system account id pool entry is invalid"));
        }
    }

    void ExternalSystemAccountIDPoolEntryFrame::ensureValid() const
    {
        return ensureValid(mExternalSystemAccountIDPoolEntry);
    }
}
