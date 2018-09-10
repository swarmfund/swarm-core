#pragma once

namespace stellar
{

class MockLedgerDelta : public LedgerDelta
{
  public:
    typedef std::set<LedgerKey, LedgerEntryIdCmp> DeletionFramesSet;
    MOCK_METHOD0(getHeader, LedgerHeader&());
    MOCK_CONST_METHOD0(getHeader, const LedgerHeader&());
    MOCK_METHOD0(getHeaderFrame, LedgerHeaderFrame&());
    MOCK_METHOD1(addEntry, void(EntryFrame const& entry));
    MOCK_METHOD1(deleteEntry, void(EntryFrame const& entry));
    MOCK_METHOD1(deleteEntry, void(LedgerKey const& key));
    MOCK_METHOD1(modEntry, void(EntryFrame const& entry));
    MOCK_METHOD1(recordEntry, void(EntryFrame const& entry));
    MOCK_METHOD1(mergeEntries, void(LedgerDelta& other));
    MOCK_METHOD0(commit, void());
    MOCK_METHOD0(rollback, void());
    MOCK_CONST_METHOD0(updateLastModified, bool());
    MOCK_CONST_METHOD1(markMeters, void(Application& app));
    MOCK_CONST_METHOD0(getLiveEntries, std::vector<LedgerEntry>());
    MOCK_CONST_METHOD0(getDeadEntries, std::vector<LedgerKey>());
    MOCK_CONST_METHOD0(getChanges, LedgerEntryChanges());
    MOCK_CONST_METHOD0(getAllChanges, const LedgerEntryChanges&());
    MOCK_CONST_METHOD1(checkAgainstDatabase, void(Application& app));
    MOCK_CONST_METHOD0(getState, KeyEntryMap());
    MOCK_CONST_METHOD0(isStateActive, bool());
    MOCK_METHOD0(getDatabase, Database&());
    MOCK_CONST_METHOD0(getPreviousFrames, const KeyEntryMap&());
    MOCK_CONST_METHOD0(getDeletionFramesSet, const DeletionFramesSet&());
    MOCK_CONST_METHOD0(getCreationFrames, const KeyEntryMap&());
    MOCK_CONST_METHOD0(getModificationFrames, const KeyEntryMap&());
};

} // namespace stellar
