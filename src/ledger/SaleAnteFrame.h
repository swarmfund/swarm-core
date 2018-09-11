#pragma once

#include "EntryFrame.h"

namespace stellar {
    class SaleAnteFrame : public EntryFrame {
        SaleAnteEntry &mSaleAnte;

        SaleAnteFrame(SaleAnteFrame const &from);

    public:
        typedef std::shared_ptr<SaleAnteFrame> pointer;

        SaleAnteFrame();

        explicit SaleAnteFrame(LedgerEntry const &from);

        SaleAnteFrame &operator=(SaleAnteFrame const &other);

        EntryFrame::pointer
        copy() const override {
            return EntryFrame::pointer(new SaleAnteFrame(*this));
        }

        // ensureValid - throws exception if entry is not valid
        static void ensureValid(SaleAnteEntry const &saleAnteEntry);

        void ensureValid() const;

        SaleAnteEntry &getSaleAnteEntry() {
            return mSaleAnte;
        }

        uint64_t getSaleID() const {
            return mSaleAnte.saleID;
        }

        BalanceID const &getParticipantBalanceID() const {
            return mSaleAnte.participantBalanceID;
        }

        uint64_t getAmount() const {
            return mSaleAnte.amount;
        }

        static pointer createNew(uint64_t saleID, BalanceID const &participantBalanceID, uint64_t amount);
    };
}