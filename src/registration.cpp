// GENERATED from `1000:8df8` by tools/gen_instructions.py - do not edit by
// hand. Regenerate with:
//
//   tools/gen_instructions.py disasm-sw-8df8.txt strings-sw-8df8.txt \
//       Registration "@@never@@" --shift 0x120 --array kRegistrationPages \
//       --count kRegistrationPageCount --include ordering.h
//
// The SHAREWARE edition's end-of-game screen - what wave 25 reaches instead of
// the registered build's Nobel ending at `1000:9499`. ONE page: the separator
// argument is deliberately an instruction that never occurs, because there is
// no page break to find. It waits with `1ac3:0b8f`, the same terminal wait the
// registered ending uses, rather than the paging wait the decks use.
//
// The two endings are not interchangeable and the port must never substitute
// one for the other: the shareware image contains no `PRIZE.GFX` and none of
// the registered ending's text, and this screen's text is absent from the
// registered image.
//
// `Lanny is 1/3 of the way to his goal` is the game stating its own wave count
// - 25 of 75 - and agreeing with the dispatch-arm count by an independent
// route.
#include "ordering.h"

namespace tubes {
namespace {
const InstructionItem kRegistration0[] = {
    {InstructionItem::kCentred, 0, 38, 159, 3, 1, "Congratulations"},   // to x 319
    {InstructionItem::kCentred, 0, 41, 159, 3, 1, "_____________"},   // to x 319
    {InstructionItem::kText, 76, 70, 155, 1, 0, "Lanny is 1/3 of the  way  to"},
    {InstructionItem::kText, 76, 80, 155, 1, 0, "his goal and he still  needs"},
    {InstructionItem::kText, 76, 90, 155, 1, 0, "your help."},
    {InstructionItem::kCentred, 0, 105, 166, 2, 0, "You can't stop now!"},   // to x 319
    {InstructionItem::kText, 76, 120, 155, 1, 0, "Register    and     continue"},
    {InstructionItem::kText, 76, 130, 155, 1, 0, "Lanny's quest for the  Nobel"},
    {InstructionItem::kText, 76, 140, 155, 1, 0, "Prize."},
};
}  // namespace

const InstructionSlide kRegistrationPages[kRegistrationPageCount] = {
    {kRegistration0, sizeof(kRegistration0) / sizeof(kRegistration0[0])},
};

}  // namespace tubes
