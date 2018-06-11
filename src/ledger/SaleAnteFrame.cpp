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
}