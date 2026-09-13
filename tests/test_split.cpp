// Pins mgmp_split.h -- who controls which cats when there are more than two
// players. In-game this needs three or four live instances AND a battle with the
// right number of human cats, so the arithmetic is pinned here and the live run
// is left to prove only what needs a game.
//
// The property that actually matters is not any single row: it is that across
// all players the ranges TILE the human cats exactly -- no cat claimed twice, no
// cat claimed by nobody. Those are the two failures CONTROL exists to catch, and
// they have opposite symptoms (a desync on the first turn versus a battle that
// stalls forever). The exhaustive sweep at the bottom is the real test; the
// hand-written rows above it are there so a wrong change says WHICH case broke.

#include "mgmp_split.h"

#include <cstdio>
#include <cstring>

using namespace mgmp;

static int failures = 0;

static void check(bool cond, const char* what) {
    if (!cond) { printf("FAIL: %s\n", what); ++failures; }
}

static void expect(uint32_t humans, uint32_t peers, uint32_t pos,
                   uint32_t start, uint32_t count) {
    SplitRange r = split_for(humans, peers, pos);
    if (r.start != start || r.count != count) {
        printf("FAIL: split_for(humans=%u, peers=%u, pos=%u) = {%u,%u}, expected {%u,%u}\n",
               humans, peers, pos, r.start, r.count, start, count);
        ++failures;
    }
}

int main() {
    // --- two players: must reproduce the old (humans+1)/2 rule exactly -------
    //
    // This is the compatibility row. The two-player split shipped and was
    // measured working over 37 turns across two battles; if a generalisation
    // changes it, the generalisation is wrong.
    expect(4, 2, 0, 0, 2);   expect(4, 2, 1, 2, 2);
    expect(3, 2, 0, 0, 2);   expect(3, 2, 1, 2, 1);   // host takes the odd one
    expect(2, 2, 0, 0, 1);   expect(2, 2, 1, 1, 1);
    expect(1, 2, 0, 0, 1);   expect(1, 2, 1, 1, 0);
    for (uint32_t h = 0; h <= 16; ++h)
        check(split_for(h, 2, 0).count == (h + 1) / 2,
              "two-player host share still equals (humans+1)/2");

    // --- three players ------------------------------------------------------
    expect(4, 3, 0, 0, 2);   expect(4, 3, 1, 2, 1);   expect(4, 3, 2, 3, 1);
    expect(3, 3, 0, 0, 1);   expect(3, 3, 1, 1, 1);   expect(3, 3, 2, 2, 1);
    expect(5, 3, 0, 0, 2);   expect(5, 3, 1, 2, 2);   expect(5, 3, 2, 4, 1);

    // --- four players, the stated goal: one cat each -------------------------
    for (uint32_t p = 0; p < 4; ++p) expect(4, 4, p, p, 1);

    // Fewer cats than players: the late positions get nothing rather than
    // sharing. A player with no cat watches; two players sharing one cat is the
    // failure this avoids.
    expect(2, 4, 0, 0, 1);   expect(2, 4, 1, 1, 1);
    expect(2, 4, 2, 2, 0);   expect(2, 4, 3, 2, 0);

    // --- degenerate inputs --------------------------------------------------
    expect(0, 4, 0, 0, 0);                  // no human cats: nobody claims one
    expect(4, 0, 0, 0, 4);                  // peers=0 is treated as 1, we take all
    expect(4, 2, 7, 0, 0);                  // pos past the end claims NOTHING
    check(split_for(4, 2, 7).count == 0,
          "an out-of-range position claims nothing rather than everything");

    // --- split_owns agrees with the range -----------------------------------
    {
        SplitRange r = split_for(4, 3, 0);   // {0,2}
        check( split_owns(r, 0), "owns first");
        check( split_owns(r, 1), "owns second");
        check(!split_owns(r, 2), "does not own third");
        SplitRange empty = split_for(2, 4, 3);
        check(!split_owns(empty, 0), "an empty range owns nothing");
    }

    // --- THE PROPERTY: the ranges tile the humans exactly --------------------
    //
    // For every plausible battle, every human cat is claimed by exactly one
    // player. This is the invariant CONTROL verifies on the wire; if it fails
    // here it would fail there as a halt or a permanent stall.
    for (uint32_t peers = 1; peers <= 8; ++peers) {
        for (uint32_t humans = 0; humans <= 24; ++humans) {
            uint32_t claims[32] = {};
            uint32_t total = 0;
            for (uint32_t pos = 0; pos < peers; ++pos) {
                SplitRange r = split_for(humans, peers, pos);
                total += r.count;
                for (uint32_t h = 0; h < humans; ++h)
                    if (split_owns(r, h)) ++claims[h];
            }
            if (total != humans) {
                printf("FAIL: humans=%u peers=%u -- shares total %u\n",
                       humans, peers, total);
                ++failures;
            }
            for (uint32_t h = 0; h < humans; ++h) {
                if (claims[h] == 1) continue;
                printf("FAIL: humans=%u peers=%u -- cat %u claimed %u time(s)\n",
                       humans, peers, h, claims[h]);
                ++failures;
                break;
            }
            // Fairness: nobody carries two more cats than anybody else.
            uint32_t lo = 0xFFFFFFFFu, hi = 0;
            for (uint32_t pos = 0; pos < peers; ++pos) {
                uint32_t c = split_for(humans, peers, pos).count;
                if (c < lo) lo = c;
                if (c > hi) hi = c;
            }
            if (hi - lo > 1) {
                printf("FAIL: humans=%u peers=%u -- shares differ by %u\n",
                       humans, peers, hi - lo);
                ++failures;
            }
            // The remainder goes to the EARLIEST positions, host first.
            for (uint32_t pos = 1; pos < peers; ++pos) {
                if (split_for(humans, peers, pos - 1).count >=
                    split_for(humans, peers, pos).count) continue;
                printf("FAIL: humans=%u peers=%u -- position %u got more than %u\n",
                       humans, peers, pos, pos - 1);
                ++failures;
                break;
            }
            // Ranges are contiguous and ascending, which is what makes the
            // roster log readable and what split_owns assumes.
            uint32_t expect_start = 0;
            for (uint32_t pos = 0; pos < peers; ++pos) {
                SplitRange r = split_for(humans, peers, pos);
                if (r.start != expect_start) {
                    printf("FAIL: humans=%u peers=%u pos=%u -- start %u, expected %u\n",
                           humans, peers, pos, r.start, expect_start);
                    ++failures;
                    break;
                }
                expect_start += r.count;
            }
        }
    }

    // --- split_rank: lineup order, roster order only as the tie-break ------
    {
        // Roster order A B C D, lineup positions 3 0 2 1 -> split order B D C A.
        const uint32_t lineup[4] = { 3, 0, 2, 1 };
        uint32_t rank[4];
        split_rank(lineup, 4, rank);
        check(rank[0] == 3 && rank[1] == 0 && rank[2] == 2 && rank[3] == 1,
              "split_rank orders by lineup position");

        // Cats outside the lineup rank after every cat in it, in roster order.
        const uint32_t mixed[4] = { kNoLineup, 1, kNoLineup, 0 };
        split_rank(mixed, 4, rank);
        check(rank[3] == 0 && rank[1] == 1 && rank[0] == 2 && rank[2] == 3,
              "cats outside the lineup rank last, in roster order");

        // Nobody in the lineup degrades to roster order exactly.
        const uint32_t none[3] = { kNoLineup, kNoLineup, kNoLineup };
        split_rank(none, 3, rank);
        check(rank[0] == 0 && rank[1] == 1 && rank[2] == 2,
              "no lineup at all is roster order");

        // Ranks are a permutation whatever the input, so split_owns still tiles.
        const uint32_t odd[6] = { 5, kNoLineup, 5, 0, 9, kNoLineup };
        split_rank(odd, 6, rank);
        bool seen[6] = {};
        bool perm = true;
        for (int i = 0; i < 6; ++i) {
            if (rank[i] >= 6 || seen[rank[i]]) perm = false; else seen[rank[i]] = true;
        }
        check(perm, "split_rank is a permutation even with duplicate positions");

        split_rank(nullptr, 0, rank);   // n = 0 must be a no-op
    }

    if (failures) { printf("test_split: FAILED -- %d failure(s)\n", failures); return 1; }
    printf("test_split: PASSED -- 0 failure(s)\n");
    return 0;
}
