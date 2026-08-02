// GENERATED from `1b2e:2d63` by tools/gen_instructions.py - do not edit by
// hand. Regenerate with:
//
//   tools/gen_instructions.py disasm-2d63.txt strings-2d63.txt Slide
//
// 21 slides, 152 strings and 22 illustrations: data, not logic, and extracted
// rather than transcribed so that no line of the game's own documentation can
// be quietly mistyped.
//
// The illustrations index the ball table at `DS:0x1da6 + 4 * type`, so
// they are atom TYPES and the port already has every sprite.
//
// THIS FILE SERVES BOTH EDITIONS. The shareware deck at `1ac3:2d0a` extracts to
// the same 21 slides and the same 174 items, and its 152-string pool is
// byte-identical - checked with the generator and again without it. See
// docs/reversing-notes.md, "The Instructions are NOT re-wrapped".
#include "instructions.h"

namespace tubes {
namespace {
const InstructionItem kSlide0[] = {
    {InstructionItem::kText, 76, 40, 150, 2, 0, "Use the  following  keys  to"},
    {InstructionItem::kText, 76, 50, 150, 2, 0, "move  around  menus,   slide"},
    {InstructionItem::kText, 76, 60, 150, 2, 0, "shows, and  everywhere  else"},
    {InstructionItem::kText, 76, 70, 150, 2, 0, "except the actual game.     "},
    {InstructionItem::kText, 76, 83, 150, 2, 0, "   ESC        Abort         "},
    {InstructionItem::kText, 76, 93, 150, 2, 0, "   Button A   Continue      "},
    {InstructionItem::kText, 76, 103, 150, 2, 0, "   Button B   Abort         "},
    {InstructionItem::kText, 76, 117, 38, 2, 0, "  The Simple Instructions   "},
    {InstructionItem::kText, 76, 130, 150, 2, 0, "Collect atoms in  test  tube"},
    {InstructionItem::kText, 76, 140, 150, 2, 0, "and match at  least  3  like"},
    {InstructionItem::kText, 76, 150, 150, 2, 0, "colours in beaker any way."},
};
const InstructionItem kSlide1[] = {
    {InstructionItem::kText, 76, 40, 38, 2, 0, "  The Detailed Instructions "},
    {InstructionItem::kAtom, 75, 50, -2, 0, 0, nullptr},
    {InstructionItem::kAtom, 75, 50, -1, 0, 0, nullptr},
    {InstructionItem::kText, 103, 65, 150, 2, 0, "The   test   tube   you"},
    {InstructionItem::kText, 103, 75, 150, 2, 0, "control to collect  and"},
    {InstructionItem::kText, 103, 85, 150, 2, 0, "release atoms can  hold"},
    {InstructionItem::kText, 103, 95, 150, 2, 0, "up to 5 atoms at a time."},
    {InstructionItem::kText, 76, 130, 150, 2, 0, "You are allowed to drop some"},
    {InstructionItem::kText, 76, 140, 150, 2, 0, "atoms  depending   on   your"},
    {InstructionItem::kText, 76, 150, 150, 2, 0, "difficulty setting."},
};
const InstructionItem kSlide2[] = {
    {InstructionItem::kText, 76, 45, 150, 2, 0, "Use Left and Right  to  move"},
    {InstructionItem::kText, 76, 55, 150, 2, 0, "the Test Tube you control."},
    {InstructionItem::kText, 76, 80, 150, 2, 0, "Press Button  A to  drop  an"},
    {InstructionItem::kText, 76, 90, 150, 2, 0, "atom into the beaker  below."},
    {InstructionItem::kText, 76, 115, 150, 2, 0, "Press Button B  or  Down  to"},
    {InstructionItem::kText, 76, 125, 150, 2, 0, "increase the  speed  of  any"},
    {InstructionItem::kText, 76, 135, 150, 2, 0, "atoms in the  tube  directly"},
    {InstructionItem::kText, 76, 145, 150, 2, 0, "above the test tube."},
};
const InstructionItem kSlide3[] = {
    {InstructionItem::kText, 76, 40, 150, 2, 0, "Molecule chains  are  formed"},
    {InstructionItem::kText, 76, 50, 150, 2, 0, "by dropping 3 or more  atoms"},
    {InstructionItem::kText, 76, 60, 150, 2, 0, "of the same  element  or  in"},
    {InstructionItem::kText, 76, 70, 150, 2, 0, "combination  with   Flashium"},
    {InstructionItem::kText, 76, 80, 150, 2, 0, "atoms in one of 4 chains:"},
    {InstructionItem::kAtom, 150, 92, 1, 0, 0, nullptr},
    {InstructionItem::kAtom, 150, 107, 1, 0, 0, nullptr},
    {InstructionItem::kAtom, 150, 122, 1, 0, 0, nullptr},
    {InstructionItem::kCentred, 0, 140, 165, 2, 0, "Vertical"},   // to x 319
    {InstructionItem::kCentred, 0, 150, 150, 2, 0, "250 Points"},   // to x 319
};
const InstructionItem kSlide4[] = {
    {InstructionItem::kAtom, 130, 40, 2, 0, 0, nullptr},
    {InstructionItem::kAtom, 150, 40, 2, 0, 0, nullptr},
    {InstructionItem::kAtom, 170, 40, 2, 0, 0, nullptr},
    {InstructionItem::kCentred, 0, 60, 165, 2, 0, "Horizontal"},   // to x 319
    {InstructionItem::kCentred, 0, 70, 150, 2, 0, "500 Points"},   // to x 319
    {InstructionItem::kAtom, 100, 85, 5, 0, 0, nullptr},
    {InstructionItem::kAtom, 115, 100, 5, 0, 0, nullptr},
    {InstructionItem::kAtom, 130, 115, 5, 0, 0, nullptr},
    {InstructionItem::kAtom, 205, 85, 6, 0, 0, nullptr},
    {InstructionItem::kAtom, 190, 100, 6, 0, 0, nullptr},
    {InstructionItem::kAtom, 175, 115, 6, 0, 0, nullptr},
    {InstructionItem::kCentred, 0, 135, 165, 2, 0, "Diagonal"},   // to x 319
    {InstructionItem::kCentred, 0, 145, 150, 2, 0, "1000 Points"},   // to x 319
};
const InstructionItem kSlide5[] = {
    {InstructionItem::kText, 76, 40, 150, 2, 0, "3 atom molecules count as  1"},
    {InstructionItem::kText, 76, 50, 150, 2, 0, "chain."},
    {InstructionItem::kText, 76, 70, 150, 2, 0, "4 atom molecules count as  2"},
    {InstructionItem::kText, 76, 80, 150, 2, 0, "chains."},
    {InstructionItem::kText, 76, 100, 150, 2, 0, "5 atom molecules count as  3"},
    {InstructionItem::kText, 76, 110, 150, 2, 0, "chains."},
    {InstructionItem::kText, 76, 130, 150, 2, 0, "Forming multiple chains  all"},
    {InstructionItem::kText, 76, 140, 150, 2, 0, "at once will create a  chain"},
    {InstructionItem::kText, 76, 150, 150, 2, 0, "bonus point multiplier!"},
};
const InstructionItem kSlide6[] = {
    {InstructionItem::kCentred, 0, 40, 38, 2, 0, "Special Atoms"},   // to x 319
    {InstructionItem::kCentred, 0, 60, 166, 2, 0, "Xenon"},   // to x 319
    {InstructionItem::kAtom, 150, 77, 11, 0, 0, nullptr},
    {InstructionItem::kText, 76, 100, 150, 2, 0, "Xenon will not bond with any"},
    {InstructionItem::kText, 76, 110, 150, 2, 0, "atom including other Xenons."},
    {InstructionItem::kText, 76, 130, 150, 2, 0, "They just take up space."},
};
const InstructionItem kSlide7[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, 0, "AntiMatter"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 9, 0, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, 0, "AntiMatter is unstable  and"},
    {InstructionItem::kText, 76, 90, 150, 2, 0, "causes surrounding atoms to"},
    {InstructionItem::kText, 76, 100, 150, 2, 0, "explode."},
    {InstructionItem::kText, 76, 120, 150, 2, 0, "Useful for removing Xenons."},
};
const InstructionItem kSlide8[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, 0, "Bonus"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 10, 0, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, 0, "The Bonus atom  turns  into"},
    {InstructionItem::kText, 76, 90, 150, 2, 0, "Flashium  when  caught  and"},
    {InstructionItem::kText, 76, 100, 150, 2, 0, "awards you an extra drop."},
    {InstructionItem::kText, 76, 120, 150, 2, 0, "Also, the Bonus Jackpot  is"},
    {InstructionItem::kText, 76, 130, 150, 2, 0, "increased  by  1000  points"},
    {InstructionItem::kText, 76, 140, 150, 2, 0, "and awarded to you."},
};
const InstructionItem kSlide9[] = {
    {InstructionItem::kCentred, 0, 40, 38, 2, 0, "Penalty Atoms"},   // to x 319
    {InstructionItem::kCentred, 0, 60, 166, 2, 0, "Multiplier"},   // to x 319
    {InstructionItem::kAtom, 150, 77, 12, 0, 0, nullptr},
    {InstructionItem::kText, 76, 100, 150, 2, 0, "Multiplier  will  fill  the"},
    {InstructionItem::kText, 76, 110, 150, 2, 0, "test tube with normal atoms."},
    {InstructionItem::kText, 76, 130, 150, 2, 0, "Drop some atoms fast before"},
    {InstructionItem::kText, 76, 140, 150, 2, 0, "another atom arrives."},
};
const InstructionItem kSlide10[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, 0, "Evil Multiplier"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 13, 0, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, 0, "Evil Multiplier  will  fill"},
    {InstructionItem::kText, 76, 90, 150, 2, 0, "the test tube with  Xenons."},
    {InstructionItem::kText, 76, 110, 150, 2, 0, "Drop the Xenons fast before"},
    {InstructionItem::kText, 76, 120, 150, 2, 0, "another atom arrives."},
};
const InstructionItem kSlide11[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, 0, "Convertor"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 14, 0, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, 0, "Convertor will  change  all"},
    {InstructionItem::kText, 76, 90, 150, 2, 0, "occurrences of the atom  it"},
    {InstructionItem::kText, 76, 100, 150, 2, 0, "lands on into Xenons."},
    {InstructionItem::kText, 76, 120, 150, 2, 0, "Drop  this  on  the   least"},
    {InstructionItem::kText, 76, 130, 150, 2, 0, "popular atom in the beaker."},
};
const InstructionItem kSlide12[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, 0, "Blocker"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 15, 0, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, 0, "Blocker will fill the beaker"},
    {InstructionItem::kText, 76, 90, 150, 2, 0, "column  it  lands  in   with"},
    {InstructionItem::kText, 76, 100, 150, 2, 0, "Xenons.    Similar  to  Evil"},
    {InstructionItem::kText, 76, 110, 150, 2, 0, "Multiplier but occurs in the"},
    {InstructionItem::kText, 76, 120, 150, 2, 0, "beaker."},
    {InstructionItem::kText, 76, 135, 150, 2, 0, "Keep AntiMatter handy,  this"},
    {InstructionItem::kText, 76, 145, 150, 2, 0, "one is trouble!"},
};
const InstructionItem kSlide13[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, 0, "Filler"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 16, 0, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, 0, "Filler permanently  adds  an"},
    {InstructionItem::kText, 76, 90, 150, 2, 0, "atom to the  bottom  of  the"},
    {InstructionItem::kText, 76, 100, 150, 2, 0, "test   tube   reducing   the"},
    {InstructionItem::kText, 76, 110, 150, 2, 0, "amount of atoms you can hold."},
    {InstructionItem::kText, 76, 130, 150, 2, 0, "Avoid this atom if you can."},
};
const InstructionItem kSlide14[] = {
    {InstructionItem::kCentred, 0, 55, 38, 2, 0, "Two, Two, Two Games in One!"},   // to x 319
    {InstructionItem::kText, 76, 75, 150, 2, 0, "Tubes  has   two   different"},
    {InstructionItem::kText, 76, 85, 150, 2, 0, "types of game play."},
    {InstructionItem::kText, 76, 105, 150, 2, 0, "However, both games  end  if"},
    {InstructionItem::kText, 76, 115, 150, 2, 0, "you  drop  more  atoms  than"},
    {InstructionItem::kText, 76, 125, 150, 2, 0, "allowed."},
};
const InstructionItem kSlide15[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, 0, "Endurance Mode"},   // to x 319
    {InstructionItem::kText, 76, 60, 150, 2, 0, "Form as many chains  as  you"},
    {InstructionItem::kText, 76, 70, 150, 2, 0, "possibly can.  The game will"},
    {InstructionItem::kText, 76, 80, 150, 2, 0, "become more  difficult  with"},
    {InstructionItem::kText, 76, 90, 150, 2, 0, "each new chain."},
    {InstructionItem::kText, 76, 110, 150, 2, 0, "Endurance  is  perfect   for"},
    {InstructionItem::kText, 76, 120, 150, 2, 0, "strategy.   You  can  design"},
    {InstructionItem::kText, 76, 130, 150, 2, 0, "elaborate  chain   reactions"},
    {InstructionItem::kText, 76, 140, 150, 2, 0, "for huge bonus points!"},
};
const InstructionItem kSlide16[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, 0, "Wave Mode"},   // to x 319
    {InstructionItem::kText, 76, 55, 150, 2, 0, "You are required to complete"},
    {InstructionItem::kText, 76, 65, 150, 2, 0, "a certain  task.  Once  that"},
    {InstructionItem::kText, 76, 75, 150, 2, 0, "occurs you  advance  to  the"},
    {InstructionItem::kText, 76, 85, 150, 2, 0, "next wave and are assigned a"},
    {InstructionItem::kText, 76, 95, 150, 2, 0, "new  task.   The  game  will"},
    {InstructionItem::kText, 76, 105, 150, 2, 0, "become more  difficult  with"},
    {InstructionItem::kText, 76, 115, 150, 2, 0, "each new wave.   Eventually,"},
    {InstructionItem::kText, 76, 125, 150, 2, 0, "atoms will already exist  in"},
    {InstructionItem::kText, 76, 135, 150, 2, 0, "the beaker at the  beginning"},
    {InstructionItem::kText, 76, 145, 150, 2, 0, "of each new wave."},
};
const InstructionItem kSlide17[] = {
    {InstructionItem::kCentred, 0, 60, 38, 2, 0, "It's too darn easy!"},   // to x 319
    {InstructionItem::kText, 76, 80, 150, 2, 0, "Tubes has  three  difficulty"},
    {InstructionItem::kText, 76, 90, 150, 2, 0, "levels. They affect how many"},
    {InstructionItem::kText, 76, 100, 150, 2, 0, "drops you  start  out  with,"},
    {InstructionItem::kText, 76, 110, 150, 2, 0, "the speed of the atoms,  and"},
    {InstructionItem::kText, 76, 120, 150, 2, 0, "how often they appear."},
};
const InstructionItem kSlide18[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, 0, "Tubes 101 (Easy)"},   // to x 319
    {InstructionItem::kText, 76, 50, 150, 2, 0, "You start with 9  drops  and"},
    {InstructionItem::kText, 76, 60, 150, 2, 0, "very slow atoms that  appear"},
    {InstructionItem::kText, 76, 70, 150, 2, 0, "infrequently."},
    {InstructionItem::kCentred, 0, 80, 166, 2, 0, "Tubes 201 (Medium)"},   // to x 319
    {InstructionItem::kText, 76, 90, 150, 2, 0, "You start with 6  drops  and"},
    {InstructionItem::kText, 76, 100, 150, 2, 0, "slow atoms that appear a bit"},
    {InstructionItem::kText, 76, 110, 150, 2, 0, "faster."},
    {InstructionItem::kCentred, 0, 120, 166, 2, 0, "Tubes 301 (Hard)"},   // to x 319
    {InstructionItem::kText, 76, 130, 150, 2, 0, "You start with 3  drops  and"},
    {InstructionItem::kText, 76, 140, 150, 2, 0, "atoms that move  and  appear"},
    {InstructionItem::kText, 76, 150, 150, 2, 0, "quickly."},
};
const InstructionItem kSlide19[] = {
    {InstructionItem::kText, 76, 55, 150, 2, 0, "The   following   keys   are"},
    {InstructionItem::kText, 76, 65, 150, 2, 0, "active, during the game:"},
    {InstructionItem::kText, 76, 80, 150, 2, 0, " ESC  Abort Game"},
    {InstructionItem::kText, 76, 90, 150, 2, 0, " F1   Help"},
    {InstructionItem::kText, 76, 100, 150, 2, 0, " F2   Save Game"},
    {InstructionItem::kText, 76, 110, 150, 2, 0, " F3   Toggle Music On/Off"},
    {InstructionItem::kText, 76, 120, 150, 2, 0, " F4   Toggle Sound FX On/Off"},
    {InstructionItem::kText, 76, 130, 150, 2, 0, " F5   Pause Game"},
};
const InstructionItem kSlide20[] = {
    {InstructionItem::kCentred, 0, 70, 166, 2, 0, "Last Slide!"},   // to x 319
    {InstructionItem::kText, 84, 100, 150, 2, 0, "Press Button A, Button B,"},
    {InstructionItem::kText, 84, 110, 150, 2, 0, "ENTER, or SPACE to exit."},
};
}  // namespace

const InstructionSlide kInstructionSlides[kInstructionSlideCount] = {
    {kSlide0, sizeof(kSlide0) / sizeof(kSlide0[0])},
    {kSlide1, sizeof(kSlide1) / sizeof(kSlide1[0])},
    {kSlide2, sizeof(kSlide2) / sizeof(kSlide2[0])},
    {kSlide3, sizeof(kSlide3) / sizeof(kSlide3[0])},
    {kSlide4, sizeof(kSlide4) / sizeof(kSlide4[0])},
    {kSlide5, sizeof(kSlide5) / sizeof(kSlide5[0])},
    {kSlide6, sizeof(kSlide6) / sizeof(kSlide6[0])},
    {kSlide7, sizeof(kSlide7) / sizeof(kSlide7[0])},
    {kSlide8, sizeof(kSlide8) / sizeof(kSlide8[0])},
    {kSlide9, sizeof(kSlide9) / sizeof(kSlide9[0])},
    {kSlide10, sizeof(kSlide10) / sizeof(kSlide10[0])},
    {kSlide11, sizeof(kSlide11) / sizeof(kSlide11[0])},
    {kSlide12, sizeof(kSlide12) / sizeof(kSlide12[0])},
    {kSlide13, sizeof(kSlide13) / sizeof(kSlide13[0])},
    {kSlide14, sizeof(kSlide14) / sizeof(kSlide14[0])},
    {kSlide15, sizeof(kSlide15) / sizeof(kSlide15[0])},
    {kSlide16, sizeof(kSlide16) / sizeof(kSlide16[0])},
    {kSlide17, sizeof(kSlide17) / sizeof(kSlide17[0])},
    {kSlide18, sizeof(kSlide18) / sizeof(kSlide18[0])},
    {kSlide19, sizeof(kSlide19) / sizeof(kSlide19[0])},
    {kSlide20, sizeof(kSlide20) / sizeof(kSlide20[0])},
};

}  // namespace tubes
