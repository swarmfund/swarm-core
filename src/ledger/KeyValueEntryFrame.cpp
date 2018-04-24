
#include "KeyValueEntryFrame.h"

namespace stellar{

    KeyValueEntryFrame::KeyValueEntryFrame():EntryFrame(LedgerEntryType::KEY_VALUE),
                                   mKeyEntry(mEntry.data.keyValue())
    {
    }

    KeyValueEntryFrame::KeyValueEntryFrame(LedgerEntry const &from) : EntryFrame(from),
                                                            mKeyEntry(mEntry.data.keyValue())
    {
        if(mKeyEntry.value.type() != KeyValueEntryType::UINT32)
            throw new std::exception();
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