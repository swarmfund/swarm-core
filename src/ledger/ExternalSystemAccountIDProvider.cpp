#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "ledger/ExternalSystemAccountIDProvider.h"
#include "lib/util/format.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

    ExternalSystemAccountIDProviderFrame::
    ExternalSystemAccountIDProviderFrame() : EntryFrame(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID_PROVIDER),
                                             mExternalSystemAccountIDProvider(mEntry.data.
                                                     externalSystemAccountIDProvider())
    {
    }

    ExternalSystemAccountIDProviderFrame::
    ExternalSystemAccountIDProviderFrame(LedgerEntry const &from)
            : EntryFrame(from), mExternalSystemAccountIDProvider(mEntry.data.externalSystemAccountIDProvider())
    {
    }

    ExternalSystemAccountIDProviderFrame::ExternalSystemAccountIDProviderFrame(
            ExternalSystemAccountIDProviderFrame const &from)
            : ExternalSystemAccountIDProviderFrame(from.mEntry)
    {
    }

    ExternalSystemAccountIDProviderFrame&
    ExternalSystemAccountIDProviderFrame::operator=(ExternalSystemAccountIDProviderFrame const &other)
    {
        if (&other != this)
        {
            mExternalSystemAccountIDProvider = other.mExternalSystemAccountIDProvider;
            mKey = other.mKey;
            mKeyCalculated = other.mKeyCalculated;
        }
        return *this;
    }

    ExternalSystemAccountIDProviderFrame::pointer
    ExternalSystemAccountIDProviderFrame::createNew(uint64 providerID, ExternalSystemType const externalSystemType,
                                                    std::string const data)
    {
        LedgerEntry le;
        le.data.type(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID_PROVIDER);
        auto& externalSystemAccountIDProvider = le.data.externalSystemAccountIDProvider();
        externalSystemAccountIDProvider.providerID = providerID;
        externalSystemAccountIDProvider.externalSystemType = externalSystemType;
        externalSystemAccountIDProvider.data = data;
        return std::make_shared<ExternalSystemAccountIDProviderFrame>(le);
    }

    bool ExternalSystemAccountIDProviderFrame::isValid(ExternalSystemAccountIDProvider const& p)
    {
        return isValidEnumValue(p.externalSystemType) && !p.data.empty();
    }

    bool ExternalSystemAccountIDProviderFrame::isValid() const
    {
        return isValid(mExternalSystemAccountIDProvider);
    }
}
