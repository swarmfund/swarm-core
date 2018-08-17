//
// Created by dmytriiev on 04.04.18.
//

#ifndef STELLAR_KYCSETTINGS_H
#define STELLAR_KYCSETTINGS_H

#include "ledger/EntryFrame.h"
#include <iostream>

namespace stellar {
    class KeyValueEntryFrame : public EntryFrame {

        KeyValueEntry& mKeyEntry;
        KeyValueEntryFrame(KeyValueEntryFrame const& from);

    public:

        typedef std::shared_ptr<KeyValueEntryFrame> pointer;

        KeyValueEntryFrame();
        KeyValueEntryFrame(LedgerEntry const& from);

        KeyValueEntryFrame& operator=(KeyValueEntryFrame const& other);

        EntryFrame::pointer copy() const override
        {
            return EntryFrame::pointer(new KeyValueEntryFrame(*this));
        }

        KeyValueEntry const& getKeyValue() const
        {
            return mKeyEntry;
        }

        KeyValueEntry& getKeyValue()
        {
            return mKeyEntry;
        }

        longstring getEntryKey()
        {
            return mKeyEntry.key;
        }

        KeyValueEntryType const&
        getKeyValueEntryType()
        {
            return mKeyEntry.value.type();
        }

        void setKey(string256 newKey)
        {
            mKeyEntry.key = newKey;
        }

        string256 getPrefix()
        {
            string256 prefix;
            std::istringstream from(mKeyEntry.key);
            std::getline(from,prefix,':');

            return prefix;
        }

    };

}

#endif //STELLAR_KYCSETTINGS_H
