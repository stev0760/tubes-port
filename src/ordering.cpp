// GENERATED from `1ac3:4889` by tools/gen_instructions.py - do not edit by
// hand. Regenerate with:
//
//   tools/gen_instructions.py disasm-sw-4889.txt strings-sw-4889.txt Ordering \
//       "CALL 0x1000:b8a8" --shift 0x120 --array kOrderingPages \
//       --count kOrderingPageCount --include ordering.h
//
// The SHAREWARE edition's "Ordering Info" deck, reachable from its menu item 8
// and run again on the way out when the player picks Exit. It is text only -
// the deck makes no call to the sprite routine at all, unlike the Instructions
// and the Credits.
//
// `--shift 0x120` is not a fudge: the shareware image's interface unit is
// larger, so every segment above it sits 0x12 paragraphs higher and the four
// text-unit entry points move by exactly that. See docs/reversing-notes.md,
// "The segment layout shifted".
//
// The page separator is `CALL 0x1000:b8a8`, which is `1ac3:0c78` - the same
// key wait every other screen in this unit uses.
#include "ordering.h"

namespace tubes {
namespace {
const InstructionItem kOrdering0[] = {
    {InstructionItem::kCentred, 0, 38, 159, 3, 1, "Register!"},   // to x 319
    {InstructionItem::kCentred, 0, 41, 159, 3, 1, "___________"},   // to x 319
    {InstructionItem::kText, 76, 60, 155, 1, 0, "You'll receive  an  enhanced"},
    {InstructionItem::kText, 76, 70, 155, 1, 0, "version with  the  following"},
    {InstructionItem::kText, 76, 80, 155, 1, 0, "additions:"},
    {InstructionItem::kCentred, 0, 100, 166, 2, 0, "50 more exciting Waves!"},   // to x 319
    {InstructionItem::kCentred, 0, 115, 166, 2, 0, "5 gorgeous new Backgrounds!"},   // to x 319
    {InstructionItem::kCentred, 0, 130, 166, 2, 0, "2 helpful new Atoms:"},   // to x 319
    {InstructionItem::kCentred, 0, 140, 38, 2, 0, "Anti-Matter And Bonus"},   // to x 319
};
const InstructionItem kOrdering1[] = {
    {InstructionItem::kCentred, 0, 38, 159, 3, 1, "Software Creations"},   // to x 319
    {InstructionItem::kCentred, 0, 41, 159, 3, 1, "_______________"},   // to x 319
    {InstructionItem::kCentred, 0, 60, 166, 2, 0, "Order by Phone"},   // to x 319
    {InstructionItem::kCentred, 0, 70, 155, 1, 0, "(508)368-8654"},   // to x 319
    {InstructionItem::kCentred, 0, 85, 166, 2, 0, "Order by Fax"},   // to x 319
    {InstructionItem::kCentred, 0, 95, 155, 1, 0, "(508)365-7214"},   // to x 319
    {InstructionItem::kCentred, 0, 110, 166, 2, 0, "Order by BBS (OPEN Door 5)"},   // to x 319
    {InstructionItem::kCentred, 0, 120, 155, 1, 0, "2400  (508)365-2359"},   // to x 319
    {InstructionItem::kCentred, 0, 130, 155, 1, 0, "14.4K (508)368-7036"},   // to x 319
    {InstructionItem::kCentred, 0, 140, 155, 1, 0, "16.8K (508)365-2032"},   // to x 319
    {InstructionItem::kCentred, 0, 150, 155, 1, 0, "28.8K (508)365-9352"},   // to x 319
};
const InstructionItem kOrdering2[] = {
    {InstructionItem::kCentred, 0, 38, 159, 3, 1, "Software Creations"},   // to x 319
    {InstructionItem::kCentred, 0, 41, 159, 3, 1, "_______________"},   // to x 319
    {InstructionItem::kText, 76, 65, 166, 2, 0, "The Software  Creations  BBS"},
    {InstructionItem::kText, 76, 75, 150, 2, 0, "is a 24 hour support  system"},
    {InstructionItem::kText, 76, 85, 150, 2, 0, "for  the  hottest  games  in"},
    {InstructionItem::kText, 76, 95, 150, 2, 0, "shareware.  You'll find  the"},
    {InstructionItem::kText, 76, 105, 150, 2, 0, "latest  in  shareware,  talk"},
    {InstructionItem::kText, 76, 115, 150, 2, 0, "with   the   authors,   meet"},
    {InstructionItem::kText, 76, 125, 150, 2, 0, "today's  leader's  in  share"},
    {InstructionItem::kText, 76, 135, 150, 2, 0, "ware  production,  and  even"},
    {InstructionItem::kText, 76, 145, 150, 2, 0, "order online!"},
};
const InstructionItem kOrdering3[] = {
    {InstructionItem::kCentred, 0, 70, 166, 2, 0, "Last Slide!"},   // to x 319
    {InstructionItem::kText, 84, 100, 150, 2, 0, "Press Button A, Button B,"},
    {InstructionItem::kText, 84, 110, 150, 2, 0, "ENTER, or SPACE to exit."},
};
}  // namespace

const InstructionSlide kOrderingPages[kOrderingPageCount] = {
    {kOrdering0, sizeof(kOrdering0) / sizeof(kOrdering0[0])},
    {kOrdering1, sizeof(kOrdering1) / sizeof(kOrdering1[0])},
    {kOrdering2, sizeof(kOrdering2) / sizeof(kOrdering2[0])},
    {kOrdering3, sizeof(kOrdering3) / sizeof(kOrdering3[0])},
};

}  // namespace tubes
