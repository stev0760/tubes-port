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
const InstructionItem kCredit0[] = {
    {InstructionItem::kCentred, 0, 38, 159, 3, 1, "Colin Buckley"},   // to x 319
    {InstructionItem::kCentred, 0, 41, 159, 3, 1, "___________"},   // to x 319
    {InstructionItem::kCentred, 0, 60, 155, 1, 0, "Executive Producer"},   // to x 319
    {InstructionItem::kCentred, 0, 70, 155, 1, 0, "Director"},   // to x 319
    {InstructionItem::kCentred, 0, 80, 155, 1, 0, "Head of Development Team"},   // to x 319
    {InstructionItem::kCentred, 0, 90, 155, 1, 0, "Co-Designer"},   // to x 319
    {InstructionItem::kCentred, 0, 100, 155, 1, 0, "Technical Director"},   // to x 319
    {InstructionItem::kCentred, 0, 110, 155, 1, 0, "Creative Consultant"},   // to x 319
    {InstructionItem::kCentred, 0, 120, 155, 1, 0, "Programmer"},   // to x 319
    {InstructionItem::kCentred, 0, 130, 155, 1, 0, "Sound"},   // to x 319
    {InstructionItem::kCentred, 0, 140, 155, 1, 0, "Gaffer"},   // to x 319
    {InstructionItem::kCentred, 0, 150, 155, 1, 0, "Aspiring Surf Punk"},   // to x 319
};
const InstructionItem kCredit1[] = {
    {InstructionItem::kCentred, 0, 38, 159, 3, 1, "Chris Blackwell"},   // to x 319
    {InstructionItem::kCentred, 0, 41, 159, 3, 1, "_____________"},   // to x 319
    {InstructionItem::kCentred, 0, 60, 155, 1, 0, "Producer"},   // to x 319
    {InstructionItem::kCentred, 0, 69, 155, 1, 0, "Co-Designer"},   // to x 319
    {InstructionItem::kCentred, 0, 79, 155, 1, 0, "Only Other Member of"},   // to x 319
    {InstructionItem::kCentred, 0, 87, 155, 1, 0, "Development Team"},   // to x 319
    {InstructionItem::kCentred, 0, 97, 155, 1, 0, "Screenplay"},   // to x 319
    {InstructionItem::kCentred, 0, 106, 155, 1, 0, "Art Director"},   // to x 319
    {InstructionItem::kCentred, 0, 115, 155, 1, 0, "Original Score"},   // to x 319
    {InstructionItem::kCentred, 0, 124, 155, 1, 0, "Creative Consultant"},   // to x 319
    {InstructionItem::kCentred, 0, 133, 155, 1, 0, "Graphic Artist"},   // to x 319
    {InstructionItem::kCentred, 0, 142, 155, 1, 0, "Key Grip"},   // to x 319
    {InstructionItem::kCentred, 0, 151, 155, 1, 0, "Resident Krunk"},   // to x 319
};
const InstructionItem kCredit2[] = {
    {InstructionItem::kCentred, 0, 58, 159, 3, 1, "Absolute Magic"},   // to x 319
    {InstructionItem::kCentred, 0, 61, 159, 3, 1, "_____________"},   // to x 319
    {InstructionItem::kText, 76, 90, 155, 1, 0, "Tubes was written in Borland"},
    {InstructionItem::kText, 76, 100, 155, 1, 0, "Pascal v7, and uses a planar"},
    {InstructionItem::kText, 76, 110, 155, 1, 0, "320x200x256 for the multiple"},
    {InstructionItem::kText, 76, 120, 155, 1, 0, "pages."},
};
const InstructionItem kCredit3[] = {
    {InstructionItem::kCentred, 0, 70, 166, 2, 0, "Last Slide!"},   // to x 319
    {InstructionItem::kText, 84, 100, 150, 2, 0, "Press Button A, Button B,"},
    {InstructionItem::kText, 84, 110, 150, 2, 0, "ENTER, or SPACE to exit."},
};
}  // namespace

const InstructionSlide kCreditPages[kCreditPageCount] = {
    {kCredit0, sizeof(kCredit0) / sizeof(kCredit0[0])},
    {kCredit1, sizeof(kCredit1) / sizeof(kCredit1[0])},
    {kCredit2, sizeof(kCredit2) / sizeof(kCredit2[0])},
    {kCredit3, sizeof(kCredit3) / sizeof(kCredit3[0])},
};

}  // namespace tubes
