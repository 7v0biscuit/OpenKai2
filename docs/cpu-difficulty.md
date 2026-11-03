# CPU difficulty: Z1, Z2 and Z3

Budokai 2 has two CPU settings in its **Options > Game Options** menu: **Game
Difficulty** for Dragon World and **COM Level** for duels. OpenKai2 adds three
harder levels on top of them, named after Budokai 3's Z1, Z2 and Z3.

Choose one on the launcher's **Settings** page under **Game > CPU difficulty**.
In game, **F6** cycles Game setting, Z1, Z2 and Z3 for the current session.

| Level | CPU behaviour | CPU hits | Damage the CPU takes | CPU Ki |
| --- | --- | --- | --- | --- |
| Game setting | As set in the game's Options | Normal | Normal | Normal |
| Z1 | Strongest | +10% | Normal | Slightly faster |
| Z2 | Strongest | +20% | −10% | Faster |
| Z3 | Strongest | +30% | −20% | Much faster |

How it works:

- Budokai 2's difficulty picks which behaviour scripts the CPU runs. Every Z
  level uses the strongest scripts (Very Hard / Very Strong) in every fight,
  whatever the in-game setting is.
- Budokai 2 has no scripts beyond Very Hard, so the Z levels also give the CPU a
  stat edge that grows with the level. The edge applies in Dragon World and in
  1P vs COM and COM vs 2P duels. In COM vs COM and other modes, only the
  behaviour changes.
- Extra damage from the CPU never knocks you out by itself: it stops at 1
  health, and the game's own hit finishes the fight. A knocked-out CPU is never
  revived.

Z levels are off in speedrun mode and in online play, so runs and online
matches play the same for everyone.
