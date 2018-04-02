//
// PROJECT:         Aspia
// FILE:            base/waitable_timer.cc
// LICENSE:         GNU Lesser General Public License 2.1
// PROGRAMMERS:     Dmitry Chapyshev (dmitry@aspia.ru)
//

#include "base/waitable_timer.h"

#undef max

namespace aspia {

WaitableTimer::~WaitableTimer()
{
    Stop();
}

// static
void NTAPI WaitableTimer::TimerProc(LPVOID context, BOOLEAN /* timer_or_wait_fired */)
{
    WaitableTimer* self = reinterpret_cast<WaitableTimer*>(context);
    Q_ASSERT(self);

    self->signal_callback_();
}

void WaitableTimer::Start(const std::chrono::milliseconds& time_delta,
                          TimeoutCallback signal_callback)
{
    Q_ASSERT(time_delta.count() < std::numeric_limits<DWORD>::max());

    if (timer_handle_)
        return;

    signal_callback_ = std::move(signal_callback);

    BOOL ret = CreateTimerQueueTimer(&timer_handle_,
                                     nullptr,
                                     TimerProc,
                                     this,
                                     static_cast<DWORD>(time_delta.count()),
                                     0,
                                     WT_EXECUTEONLYONCE);
    if (!ret)
        qFatal("CreateTimerQueueTimer failed");
}

void WaitableTimer::Stop()
{
    if (timer_handle_)
    {
        DeleteTimerQueueTimer(nullptr, timer_handle_, INVALID_HANDLE_VALUE);
        timer_handle_ = nullptr;
    }
}

bool WaitableTimer::IsActive() const
{
    return timer_handle_ != nullptr;
}

} // namespace aspia
