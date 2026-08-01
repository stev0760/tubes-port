// GENERATED from `1b2e:2d63` by tools/gen_instructions.py - do not edit
// by hand. 21 slides, 152 strings and 22 illustrations: data, not
// logic, and extracted rather than transcribed so that no line of the
// game's own documentation can be quietly mistyped.
//
// The illustrations index the ball table at `DS:0x1da6 + 4 * type`, so
// they are atom TYPES and the port already has every sprite.
#include "instructions.h"

namespace tubes {
namespace {
const InstructionItem kSlide0[] = {
    {InstructionItem::kCentred, 0, 184, 118, 2, "Up - Previous Slide"},   // to x 319
    {InstructionItem::kCentred, 0, 192, 118, 2, "Down - Next Slide"},   // to x 319
    {InstructionItem::kText, 76, 40, 150, 2, "Use the  following  keys  to"},
    {InstructionItem::kText, 76, 50, 150, 2, "move  around  menus,   slide"},
    {InstructionItem::kText, 76, 60, 150, 2, "shows, and  everywhere  else"},
    {InstructionItem::kText, 76, 70, 150, 2, "except the actual game.     "},
    {InstructionItem::kText, 76, 83, 150, 2, "   ESC        Abort         "},
    {InstructionItem::kText, 76, 93, 150, 2, "   Button A   Continue      "},
    {InstructionItem::kText, 76, 103, 150, 2, "   Button B   Abort         "},
    {InstructionItem::kText, 76, 117, 38, 2, "  The Simple Instructions   "},
    {InstructionItem::kText, 76, 130, 150, 2, "Collect atoms in  test  tube"},
    {InstructionItem::kText, 76, 140, 150, 2, "and match at  least  3  like"},
    {InstructionItem::kText, 76, 150, 150, 2, "colours in beaker any way."},
};
const InstructionItem kSlide1[] = {
    {InstructionItem::kText, 76, 40, 38, 2, "  The Detailed Instructions "},
    {InstructionItem::kAtom, 75, 50, -1, 0, nullptr},
    {InstructionItem::kAtom, 75, 50, -2, 0, nullptr},
    {InstructionItem::kText, 103, 65, 150, 2, "The   test   tube   you"},
    {InstructionItem::kText, 103, 75, 150, 2, "control to collect  and"},
    {InstructionItem::kText, 103, 85, 150, 2, "release atoms can  hold"},
    {InstructionItem::kText, 103, 95, 150, 2, "up to 5 atoms at a time."},
    {InstructionItem::kText, 76, 130, 150, 2, "You are allowed to drop some"},
    {InstructionItem::kText, 76, 140, 150, 2, "atoms  depending   on   your"},
    {InstructionItem::kText, 76, 150, 150, 2, "difficulty setting."},
};
const InstructionItem kSlide2[] = {
    {InstructionItem::kText, 76, 45, 150, 2, "Use Left and Right  to  move"},
    {InstructionItem::kText, 76, 55, 150, 2, "the Test Tube you control."},
    {InstructionItem::kText, 76, 80, 150, 2, "Press Button  A to  drop  an"},
    {InstructionItem::kText, 76, 90, 150, 2, "atom into the beaker  below."},
    {InstructionItem::kText, 76, 115, 150, 2, "Press Button B  or  Down  to"},
    {InstructionItem::kText, 76, 125, 150, 2, "increase the  speed  of  any"},
    {InstructionItem::kText, 76, 135, 150, 2, "atoms in the  tube  directly"},
    {InstructionItem::kText, 76, 145, 150, 2, "above the test tube."},
};
const InstructionItem kSlide3[] = {
    {InstructionItem::kText, 76, 40, 150, 2, "Molecule chains  are  formed"},
    {InstructionItem::kText, 76, 50, 150, 2, "by dropping 3 or more  atoms"},
    {InstructionItem::kText, 76, 60, 150, 2, "of the same  element  or  in"},
    {InstructionItem::kText, 76, 70, 150, 2, "combination  with   Flashium"},
    {InstructionItem::kText, 76, 80, 150, 2, "atoms in one of 4 chains:"},
    {InstructionItem::kAtom, 150, 92, 1, 0, nullptr},
    {InstructionItem::kAtom, 150, 107, 1, 0, nullptr},
    {InstructionItem::kAtom, 150, 122, 1, 0, nullptr},
    {InstructionItem::kCentred, 0, 140, 165, 2, "Vertical"},   // to x 319
    {InstructionItem::kCentred, 0, 150, 150, 2, "250 Points"},   // to x 319
};
const InstructionItem kSlide4[] = {
    {InstructionItem::kAtom, 130, 40, 2, 0, nullptr},
    {InstructionItem::kAtom, 150, 40, 2, 0, nullptr},
    {InstructionItem::kAtom, 170, 40, 2, 0, nullptr},
    {InstructionItem::kCentred, 0, 60, 165, 2, "Horizontal"},   // to x 319
    {InstructionItem::kCentred, 0, 70, 150, 2, "500 Points"},   // to x 319
    {InstructionItem::kAtom, 100, 85, 5, 0, nullptr},
    {InstructionItem::kAtom, 115, 100, 5, 0, nullptr},
    {InstructionItem::kAtom, 130, 115, 5, 0, nullptr},
    {InstructionItem::kAtom, 205, 85, 6, 0, nullptr},
    {InstructionItem::kAtom, 190, 100, 6, 0, nullptr},
    {InstructionItem::kAtom, 175, 115, 6, 0, nullptr},
    {InstructionItem::kCentred, 0, 135, 165, 2, "Diagonal"},   // to x 319
    {InstructionItem::kCentred, 0, 145, 150, 2, "1000 Points"},   // to x 319
};
const InstructionItem kSlide5[] = {
    {InstructionItem::kText, 76, 40, 150, 2, "3 atom molecules count as  1"},
    {InstructionItem::kText, 76, 50, 150, 2, "chain."},
    {InstructionItem::kText, 76, 70, 150, 2, "4 atom molecules count as  2"},
    {InstructionItem::kText, 76, 80, 150, 2, "chains."},
    {InstructionItem::kText, 76, 100, 150, 2, "5 atom molecules count as  3"},
    {InstructionItem::kText, 76, 110, 150, 2, "chains."},
    {InstructionItem::kText, 76, 130, 150, 2, "Forming multiple chains  all"},
    {InstructionItem::kText, 76, 140, 150, 2, "at once will create a  chain"},
    {InstructionItem::kText, 76, 150, 150, 2, "bonus point multiplier!"},
};
const InstructionItem kSlide6[] = {
    {InstructionItem::kCentred, 0, 40, 38, 2, "Special Atoms"},   // to x 319
    {InstructionItem::kCentred, 0, 60, 166, 2, "Xenon"},   // to x 319
    {InstructionItem::kAtom, 150, 77, 11, 0, nullptr},
    {InstructionItem::kText, 76, 100, 150, 2, "Xenon will not bond with any"},
    {InstructionItem::kText, 76, 110, 150, 2, "atom including other Xenons."},
    {InstructionItem::kText, 76, 130, 150, 2, "They just take up space."},
};
const InstructionItem kSlide7[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, "AntiMatter"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 9, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, "AntiMatter is unstable  and"},
    {InstructionItem::kText, 76, 90, 150, 2, "causes surrounding atoms to"},
    {InstructionItem::kText, 76, 100, 150, 2, "explode."},
    {InstructionItem::kText, 76, 120, 150, 2, "Useful for removing Xenons."},
};
const InstructionItem kSlide8[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, "Bonus"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 10, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, "The Bonus atom  turns  into"},
    {InstructionItem::kText, 76, 90, 150, 2, "Flashium  when  caught  and"},
    {InstructionItem::kText, 76, 100, 150, 2, "awards you an extra drop."},
    {InstructionItem::kText, 76, 120, 150, 2, "Also, the Bonus Jackpot  is"},
    {InstructionItem::kText, 76, 130, 150, 2, "increased  by  1000  points"},
    {InstructionItem::kText, 76, 140, 150, 2, "and awarded to you."},
};
const InstructionItem kSlide9[] = {
    {InstructionItem::kCentred, 0, 40, 38, 2, "Penalty Atoms"},   // to x 319
    {InstructionItem::kCentred, 0, 60, 166, 2, "Multiplier"},   // to x 319
    {InstructionItem::kAtom, 150, 77, 12, 0, nullptr},
    {InstructionItem::kText, 76, 100, 150, 2, "Multiplier  will  fill  the"},
    {InstructionItem::kText, 76, 110, 150, 2, "test tube with normal atoms."},
    {InstructionItem::kText, 76, 130, 150, 2, "Drop some atoms fast before"},
    {InstructionItem::kText, 76, 140, 150, 2, "another atom arrives."},
};
const InstructionItem kSlide10[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, "Evil Multiplier"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 13, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, "Evil Multiplier  will  fill"},
    {InstructionItem::kText, 76, 90, 150, 2, "the test tube with  Xenons."},
    {InstructionItem::kText, 76, 110, 150, 2, "Drop the Xenons fast before"},
    {InstructionItem::kText, 76, 120, 150, 2, "another atom arrives."},
};
const InstructionItem kSlide11[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, "Convertor"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 14, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, "Convertor will  change  all"},
    {InstructionItem::kText, 76, 90, 150, 2, "occurrences of the atom  it"},
    {InstructionItem::kText, 76, 100, 150, 2, "lands on into Xenons."},
    {InstructionItem::kText, 76, 120, 150, 2, "Drop  this  on  the   least"},
    {InstructionItem::kText, 76, 130, 150, 2, "popular atom in the beaker."},
};
const InstructionItem kSlide12[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, "Blocker"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 15, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, "Blocker will fill the beaker"},
    {InstructionItem::kText, 76, 90, 150, 2, "column  it  lands  in   with"},
    {InstructionItem::kText, 76, 100, 150, 2, "Xenons.    Similar  to  Evil"},
    {InstructionItem::kText, 76, 110, 150, 2, "Multiplier but occurs in the"},
    {InstructionItem::kText, 76, 120, 150, 2, "beaker."},
    {InstructionItem::kText, 76, 135, 150, 2, "Keep AntiMatter handy,  this"},
    {InstructionItem::kText, 76, 145, 150, 2, "one is trouble!"},
};
const InstructionItem kSlide13[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, "Filler"},   // to x 319
    {InstructionItem::kAtom, 150, 57, 16, 0, nullptr},
    {InstructionItem::kText, 76, 80, 150, 2, "Filler permanently  adds  an"},
    {InstructionItem::kText, 76, 90, 150, 2, "atom to the  bottom  of  the"},
    {InstructionItem::kText, 76, 100, 150, 2, "test   tube   reducing   the"},
    {InstructionItem::kText, 76, 110, 150, 2, "amount of atoms you can hold."},
    {InstructionItem::kText, 76, 130, 150, 2, "Avoid this atom if you can."},
};
const InstructionItem kSlide14[] = {
    {InstructionItem::kCentred, 0, 55, 38, 2, "Two, Two, Two Games in One!"},   // to x 319
    {InstructionItem::kText, 76, 75, 150, 2, "Tubes  has   two   different"},
    {InstructionItem::kText, 76, 85, 150, 2, "types of game play."},
    {InstructionItem::kText, 76, 105, 150, 2, "However, both games  end  if"},
    {InstructionItem::kText, 76, 115, 150, 2, "you  drop  more  atoms  than"},
    {InstructionItem::kText, 76, 125, 150, 2, "allowed."},
};
const InstructionItem kSlide15[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, "Endurance Mode"},   // to x 319
    {InstructionItem::kText, 76, 60, 150, 2, "Form as many chains  as  you"},
    {InstructionItem::kText, 76, 70, 150, 2, "possibly can.  The game will"},
    {InstructionItem::kText, 76, 80, 150, 2, "become more  difficult  with"},
    {InstructionItem::kText, 76, 90, 150, 2, "each new chain."},
    {InstructionItem::kText, 76, 110, 150, 2, "Endurance  is  perfect   for"},
    {InstructionItem::kText, 76, 120, 150, 2, "strategy.   You  can  design"},
    {InstructionItem::kText, 76, 130, 150, 2, "elaborate  chain   reactions"},
    {InstructionItem::kText, 76, 140, 150, 2, "for huge bonus points!"},
};
const InstructionItem kSlide16[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, "Wave Mode"},   // to x 319
    {InstructionItem::kText, 76, 55, 150, 2, "You are required to complete"},
    {InstructionItem::kText, 76, 65, 150, 2, "a certain  task.  Once  that"},
    {InstructionItem::kText, 76, 75, 150, 2, "occurs you  advance  to  the"},
    {InstructionItem::kText, 76, 85, 150, 2, "next wave and are assigned a"},
    {InstructionItem::kText, 76, 95, 150, 2, "new  task.   The  game  will"},
    {InstructionItem::kText, 76, 105, 150, 2, "become more  difficult  with"},
    {InstructionItem::kText, 76, 115, 150, 2, "each new wave.   Eventually,"},
    {InstructionItem::kText, 76, 125, 150, 2, "atoms will already exist  in"},
    {InstructionItem::kText, 76, 135, 150, 2, "the beaker at the  beginning"},
    {InstructionItem::kText, 76, 145, 150, 2, "of each new wave."},
};
const InstructionItem kSlide17[] = {
    {InstructionItem::kCentred, 0, 60, 38, 2, "It's too darn easy!"},   // to x 319
    {InstructionItem::kText, 76, 80, 150, 2, "Tubes has  three  difficulty"},
    {InstructionItem::kText, 76, 90, 150, 2, "levels. They affect how many"},
    {InstructionItem::kText, 76, 100, 150, 2, "drops you  start  out  with,"},
    {InstructionItem::kText, 76, 110, 150, 2, "the speed of the atoms,  and"},
    {InstructionItem::kText, 76, 120, 150, 2, "how often they appear."},
};
const InstructionItem kSlide18[] = {
    {InstructionItem::kCentred, 0, 40, 166, 2, "Tubes 101 (Easy)"},   // to x 319
    {InstructionItem::kText, 76, 50, 150, 2, "You start with 9  drops  and"},
    {InstructionItem::kText, 76, 60, 150, 2, "very slow atoms that  appear"},
    {InstructionItem::kText, 76, 70, 150, 2, "infrequently."},
    {InstructionItem::kCentred, 0, 80, 166, 2, "Tubes 201 (Medium)"},   // to x 319
    {InstructionItem::kText, 76, 90, 150, 2, "You start with 6  drops  and"},
    {InstructionItem::kText, 76, 100, 150, 2, "slow atoms that appear a bit"},
    {InstructionItem::kText, 76, 110, 150, 2, "faster."},
    {InstructionItem::kCentred, 0, 120, 166, 2, "Tubes 301 (Hard)"},   // to x 319
    {InstructionItem::kText, 76, 130, 150, 2, "You start with 3  drops  and"},
    {InstructionItem::kText, 76, 140, 150, 2, "atoms that move  and  appear"},
    {InstructionItem::kText, 76, 150, 150, 2, "quickly."},
};
const InstructionItem kSlide19[] = {
    {InstructionItem::kText, 76, 55, 150, 2, "The   following   keys   are"},
    {InstructionItem::kText, 76, 65, 150, 2, "active, during the game:"},
    {InstructionItem::kText, 76, 80, 150, 2, " ESC  Abort Game"},
    {InstructionItem::kText, 76, 90, 150, 2, " F1   Help"},
    {InstructionItem::kText, 76, 100, 150, 2, " F2   Save Game"},
    {InstructionItem::kText, 76, 110, 150, 2, " F3   Toggle Music On/Off"},
    {InstructionItem::kText, 76, 120, 150, 2, " F4   Toggle Sound FX On/Off"},
    {InstructionItem::kText, 76, 130, 150, 2, " F5   Pause Game"},
};
const InstructionItem kSlide20[] = {
    {InstructionItem::kCentred, 0, 70, 166, 2, "Last Slide!"},   // to x 319
    {InstructionItem::kText, 84, 100, 150, 2, "Press Button A, Button B,"},
    {InstructionItem::kText, 84, 110, 150, 2, "ENTER, or SPACE to exit."},
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
