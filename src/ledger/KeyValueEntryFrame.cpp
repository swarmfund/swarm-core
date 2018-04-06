//
// Created by dmytriiev on 04.04.18.
//

#include "KeyValueEntryFrame.h"

namespace stellar{

    KeyValueEntryFrame::KeyValueEntryFrame():EntryFrame(LedgerEntryType::KEY_VALUE),
                                   mKeyEntry(mEntry.data.keyValue())
    {
    }

    KeyValueEntryFrame::KeyValueEntryFrame(LedgerEntry const &from) : EntryFrame(from),
                                                            mKeyEntry(mEntry.data.keyValue())
    {
    }

    KeyValueEntryFrame::KeyValueEntryFrame(KeyValueEntryFrame const &from) : KeyValueEntryFrame(from.mEntry)
    {
    }

    KeyValueEntryFrame &KeyValueEntryFrame::operator=(KeyValueEntryFrame const &other) {
        if(&other != this)
        {
            mKey = other.mKey;
            mKeyCalculated = other.mKeyCalculated;
            mKeyEntry = other.mKeyEntry;
        }
        return *this;
    }

}