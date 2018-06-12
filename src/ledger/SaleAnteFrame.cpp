#include "SaleAnteFrame.h"

using namespace std;

namespace stellar {
    SaleAnteFrame::SaleAnteFrame() : EntryFrame(LedgerEntryType::SALE_ANTE), mSaleAnte(mEntry.data.saleAnte()) {}

    SaleAnteFrame::SaleAnteFrame(const LedgerEntry &from) : EntryFrame(from), mSaleAnte(mEntry.data.saleAnte()) {}

    SaleAnteFrame::SaleAnteFrame(const SaleAnteFrame &from) : SaleAnteFrame(from.mEntry) {}

    SaleAnteFrame &SaleAnteFrame::operator=(SaleAnteFrame const &other) {
        if (&other != this) {
            mSaleAnte = other.mSaleAnte;
            mKey = other.mKey;
            mKeyCalculated = other.mKeyCalculated;
        }
        return *this;
    }

    void SaleAnteFrame::ensureValid(SaleAnteEntry const &saleAnteEntry) {

    }

    void SaleAnteFrame::ensureValid() const {
        ensureValid(mSaleAnte);
    }

    SaleAnteFrame::pointer
    SaleAnteFrame::createNew(uint64_t saleID, BalanceID const &participantBalanceID, uint64_t amount) {
        LedgerEntry entry;
        entry.data.type(LedgerEntryType::SALE_ANTE);
        auto &saleAnte = entry.data.saleAnte();
        saleAnte.saleID = saleID;
        saleAnte.participantBalanceID = participantBalanceID;
        saleAnte.amount = amount;

        return make_shared<SaleAnteFrame>(entry);
    }
}