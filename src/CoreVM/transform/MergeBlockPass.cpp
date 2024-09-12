// SPDX-License-Identifier: Apache-2.0
module;

#include <cstdio>
#include <list>

module CoreVM;
namespace CoreVM
{

static bool isSameInstructions(BasicBlock* a, BasicBlock* b)
{
    if (a->size() != b->size())
        return false;

    for (size_t i = 0, e = a->size(); i != e; ++i)
        if (!IsSameInstruction::test(a->instruction(i), b->instruction(i)))
            return false;

    return true;
}

static bool isSameSuccessors(BasicBlock* a, BasicBlock* b)
{
    if (a->successors().size() != b->successors().size())
        return false;

    for (size_t i = 0, e = a->successors().size(); i != e; ++i)
        if (a->successors()[i] != b->successors()[i])
            return false;

    return true;
}

bool mergeSameBlocks(IRHandler* handler)
{
    std::list<std::list<BasicBlock*>> uniques;

    for (BasicBlock* bb: handler->basicBlocks())
    {
        bool found = false;
        // check if we already have a BB that is equal
        for (auto& uniq: uniques)
        {
            for (BasicBlock* otherBB: uniq)
            {
                if (isSameInstructions(bb, otherBB) && isSameSuccessors(bb, otherBB))
                {
                    uniq.emplace_back(bb);
                    found = true;
                    break;
                }
            }
            if (found)
            {
                break;
            }
        }
        if (!found)
        {
            // add new list and add himself to it
            uniques.push_back({ bb });
        }
    }

    for (std::list<BasicBlock*>& uniq: uniques)
    {
        if (uniq.size() > 1)
        {
            for (auto i = ++uniq.begin(), e = uniq.end(); i != e; ++i)
            {
                for (BasicBlock* pred: (*i)->predecessors())
                {
                    pred->getTerminator()->replaceOperand(*i, uniq.front());
                }
            }
        }
    }

    return false;
}

} // namespace CoreVM
