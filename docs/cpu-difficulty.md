# CPU difficulty: Z1 to Z6

Budokai 2 has two CPU settings in its **Options > Game Options** menu: **Game
Difficulty** for Dragon World and **COM Level** for duels. OpenKai2 adds three
six harder levels on top of them. Z1 to Z3 are named after Budokai 3's levels;
Z4 to Z6 go further for experienced players.

Choose one on the launcher's **Settings** page under **Game > CPU difficulty**.
In game, **F6** cycles Game setting and Z1 to Z6 for the current session.

| Level | CPU behaviour | CPU hits | Damage the CPU takes | CPU Ki | Starting Ki | Health recovery |
| --- | --- | --- | --- | --- | --- | --- |
| Game setting | As set in the game's Options | Normal | Normal | Normal | Normal | None |
| Z1 | Strongest | +10% | Normal | Slightly faster | Normal | None |
| Z2 | Strongest | +20% | −10% | Faster | 10% | None |
| Z3 | Strongest | +30% | −20% | Much faster | 25% | None |
| Z4 | Strongest | +40% | −30% | Faster still | 40% | About 0.3% of its health a second |
| Z5 | Strongest | +55% | −40% | Very fast | 60% | About 0.6% a second |
| Z6 | Strongest | +70% | −50% | Fastest | Full | About 0.9% a second |

How it works:

- Budokai 2's difficulty picks which behaviour scripts the CPU runs. Every Z
  level uses the strongest scripts (Very Hard / Very Strong) in every fight,
  whatever the in-game setting is.
- Budokai 2 has no scripts beyond Very Hard, so the Z levels also give the CPU a
  stat edge that grows with the level. The edge applies in Dragon World and in
  1P vs COM and COM vs 2P duels, and against CPU opponents in World Tournament.
  Tournament tiers and rounds keep their original values. Human-versus-human
  matches receive no stat changes; two CPU opponents receive the same Ki bonus
  without a damage advantage for either side.
- Extra damage from the CPU never knocks you out by itself: it stops at 1
  health, and the game's own hit finishes the fight. A knocked-out CPU is never
  revived, and health recovery stops once it is knocked out.
- Starting Ki is given at the start of each fight. Health recovery only applies
  when one side is human, so two CPU opponents are never healed.

Z levels are off in online play, so online matches play the same for everyone.
In speedrun mode they are allowed and each one is its own category; see
[Speedrun mode](speedrun.md). The level is locked for a run, so F6 does nothing
there.

The startup log records each battle's mode, CPU sides and Z level. Tournament
entries also include the tier and round; AI initialization records the preset
selected from the original game data. These entries distinguish a selected
launcher option from a modifier applied to an actual fight.
