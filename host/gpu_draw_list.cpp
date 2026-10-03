#include "gpu_draw_list.h"

void FrameDrawList::AddDrawCommand(DrawCommand&& cmd)
{
    std::lock_guard<std::mutex> lock(mutex_);
    building_.push_back(std::move(cmd));
}

void FrameDrawList::SwapReady()
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Found live during final-review fix verification (beyond what the
    // review itself flagged): VdSwap can call SwapReady many times
    // between two render-thread TakeReady calls (the guest CPU thread's
    // VdSwap rate and the display link's render rate are independent).
    // Live-observed every run: a real, non-empty batch gets built, one
    // SwapReady moves it into ready_, and a SECOND SwapReady -- with
    // nothing new since -- fires before the render thread ever calls
    // TakeReady, unconditionally overwriting ready_ with an empty list
    // and permanently destroying the only real content this whole
    // sub-project exists to show. Only replace ready_ when there is
    // something new to show; an empty building_ means "nothing new
    // happened," not "show nothing" -- the render thread's own TakeReady
    // is what decides a batch has been consumed.
    if (!building_.empty())
    {
        ready_ = std::move(building_);
    }
    building_.clear();
}

std::vector<DrawCommand> FrameDrawList::TakeReady()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DrawCommand> result = std::move(ready_);
    ready_.clear();
    return result;
}
