// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/ir/BasicBlock.h>
#include <CoreVM/ir/IRHandler.h>
#include <CoreVM/ir/Instructions.h>
#include <CoreVM/transform/EmptyBlockElimination.h>

#include <list>

namespace CoreVM::transform
{

bool emptyBlockElimination(IRHandler* handler)
{
    std::list<BasicBlock*> eliminated;

    for (BasicBlock* bb: handler->basicBlocks())
    {
        if (bb->size() != 1)
            continue;

        if (BrInstr* br = dynamic_cast<BrInstr*>(bb->getTerminator()))
        {
            BasicBlock* newSuccessor = br->targetBlock();
            eliminated.push_back(bb);
            if (bb == handler->getEntryBlock())
            {
                handler->setEntryBlock(bb);
                break;
            }
            else
            {
                for (BasicBlock* pred: bb->predecessors())
                {
                    pred->getTerminator()->replaceOperand(bb, newSuccessor);
                }
            }
        }
    }

    for (BasicBlock* bb: eliminated)
    {
        bb->getHandler()->erase(bb);
    }

    return eliminated.size() > 0;
}

} // namespace CoreVM::transform
