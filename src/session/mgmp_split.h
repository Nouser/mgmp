#pragma once
// mgmp_split -- who controls which cats, as arithmetic.
//
// Extracted for the same reason td_decide and barrier_decide are: exercising it
// in the game needs three or four live instances and a battle with the right
// number of human cats, which is an expensive way to find out that a division
// rounded the wrong way. Here it is a table.
//
// THE RULE. The battle's human-driven cats are handed out in LINEUP order
// (see split_rank below -- the party's order in the run, not the battle's
// roster order), contiguously, one run per player, earliest positions first. Every player gets
// floor(humans/P), and the first (humans % P) players get one extra -- so the
// remainder lands on the host and the players nearest it, which is the same
// preference the two-player rule had when it gave the host the odd cat.
//
//   4 cats, 2 players  ->  2 / 2
//   3 cats, 2 players  ->  2 / 1        (the old (humans+1)/2, unchanged)
//   4 cats, 3 players  ->  2 / 1 / 1
//   4 cats, 4 players  ->  1 / 1 / 1 / 1
//   2 cats, 4 players  ->  1 / 1 / 0 / 0
//
// POSITION, NOT PEER ID. `pos` is an index into the sorted membership list.
// The two are the same until somebody disconnects, after which ids have a gap
// and positions do not. Deriving the split from ids would silently shift every
// later player's cats the moment an earlier one dropped -- and shifting cats
// mid-run means two players briefly claim the same one, which is a desync on
// its first turn.
//
// CONTIGUOUS, NOT INTERLEAVED. Adjacent cats per player make the roster log
// readable by eye, which is how every control bug so far has actually been
// spotted. Interleaving would spread one player's cats across the whole list
// for no gain.
//
// The result is still cross-checked on the wire: every player publishes what it
// claims in CONTROL, and the tally must show each human cat claimed by exactly
// one player. This function being right is not what makes the split safe; it is
// what makes it agree without negotiating.

#include <cstdint>

namespace mgmp {

struct SplitRange {
    uint32_t start = 0;   // index into the HUMAN cats, in roster order
    uint32_t count = 0;   // how many of them are this peer's
};

// `humans` is how many cats a human brain drives in this battle, `peers` how
// many players are in the session, `pos` this peer's position in the sorted
// membership list. A peers of 0 is treated as 1, and a pos past the end is
// clamped to claiming nothing -- both are "we have not been told the membership
// yet", where claiming nothing is the safe answer and claiming everything is
// the one that has two people driving one cat.
inline SplitRange split_for(uint32_t humans, uint32_t peers, uint32_t pos) {
    SplitRange r;
    if (peers == 0) peers = 1;
    if (pos >= peers) return r;              // start 0, count 0

    const uint32_t base  = humans / peers;
    const uint32_t extra = humans % peers;

    // Everyone before us took base, plus one each for the first `extra` of them.
    r.start = pos * base + (pos < extra ? pos : extra);
    r.count = base + (pos < extra ? 1u : 0u);
    return r;
}

// True when cat number `nth_human` (0-based, in roster order) belongs to us.
inline bool split_owns(const SplitRange& r, uint32_t nth_human) {
    return nth_human >= r.start && nth_human < r.start + r.count;
}

// THE ORDER the human cats are walked in. Roster order was the original rule,
// and the game rebuilds the roster for every battle -- so which cats fell in
// the host's half changed from fight to fight, with nothing the players could
// do about it. The run's own cat list (the party lineup, MewDirector+1472) is
// stable for the whole adventure, so the humans are ranked by their position
// in it and the split walks THAT order: the first half of the party is the
// host's, and arranging the party IS choosing the split.
//
// A human cat that is not in the lineup at all (kNoLineup -- a summon, a
// possessed enemy, or a build where the CatData link did not resolve) ranks
// after every cat that is, in roster order, so it still gets exactly one owner
// and never displaces a party cat. When NO cat is in the lineup this degrades
// to the old roster-order rule, which is what makes it safe to ship without a
// switch.
//
// `lineup[i]` is the lineup position of the i-th HUMAN cat in roster order, or
// kNoLineup; `rank[i]` receives that cat's position in the split order, which
// is what split_owns takes. Pure, so the tests can pin it; n is at most
// kMaxCats, so the quadratic ranking is fine.
constexpr uint32_t kNoLineup = 0xFFFFFFFFu;

inline void split_rank(const uint32_t* lineup, uint32_t n, uint32_t* rank) {
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t r = 0;
        for (uint32_t j = 0; j < n; ++j) {
            if (j == i) continue;
            // j precedes i on a smaller lineup position, or on the same one
            // (only possible for two kNoLineup cats) when it is earlier in
            // the roster.
            if (lineup[j] < lineup[i] || (lineup[j] == lineup[i] && j < i)) ++r;
        }
        rank[i] = r;
    }
}

} // namespace mgmp
