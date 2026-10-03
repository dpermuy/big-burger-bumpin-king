#include "gpu_draw_list.h"

void FrameDrawList::AddDrawCommand(DrawCommand&& cmd)
{
    std::lock_guard<std::mutex> lock(mutex_);
    building_.push_back(std::move(cmd));
}

void FrameDrawList::SwapReady()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = std::move(building_);
    building_.clear();
}

std::vector<DrawCommand> FrameDrawList::TakeReady()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DrawCommand> result = std::move(ready_);
    ready_.clear();
    return result;
}
