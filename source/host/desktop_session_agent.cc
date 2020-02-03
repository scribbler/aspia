//
// Aspia Project
// Copyright (C) 2020 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "host/desktop_session_agent.h"

#include "base/logging.h"
#include "base/power_controller.h"
#include "base/threading/thread.h"
#include "codec/video_util.h"
#include "common/message_serialization.h"
#include "desktop/capture_scheduler.h"
#include "desktop/screen_capturer_wrapper.h"
#include "desktop/shared_desktop_frame.h"
#include "host/input_injector_win.h"
#include "ipc/ipc_channel.h"
#include "ipc/shared_memory.h"

namespace host {

DesktopSessionAgent::DesktopSessionAgent(std::shared_ptr<base::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner))
{
    // Nothing
}

DesktopSessionAgent::~DesktopSessionAgent() = default;

void DesktopSessionAgent::start(std::u16string_view channel_id)
{
    channel_ = std::make_unique<ipc::Channel>();

    if (!channel_->connect(channel_id))
        return;

    channel_->setListener(this);
    channel_->resume();
}

void DesktopSessionAgent::onDisconnected()
{
    stopSession();
    task_runner_->postQuit();
}

void DesktopSessionAgent::onMessageReceived(const base::ByteArray& buffer)
{
    incoming_message_.Clear();

    if (!common::parseMessage(buffer, &incoming_message_))
    {
        LOG(LS_ERROR) << "Invalid message from service";
        return;
    }

    if (incoming_message_.has_encode_frame_result())
    {
        captureEnd();
    }
    else if (incoming_message_.has_pointer_event())
    {
        if (input_injector_)
            input_injector_->injectPointerEvent(incoming_message_.pointer_event());
    }
    else if (incoming_message_.has_key_event())
    {
        if (input_injector_)
            input_injector_->injectKeyEvent(incoming_message_.key_event());
    }
    else if (incoming_message_.has_clipboard_event())
    {
        if (clipboard_monitor_)
            clipboard_monitor_->injectClipboardEvent(incoming_message_.clipboard_event());
    }
    else if (incoming_message_.has_start_session())
    {
        startSession();
    }
    else if (incoming_message_.has_stop_session())
    {
        stopSession();
    }
    else if (incoming_message_.has_select_source())
    {
        if (screen_capturer_)
            screen_capturer_->selectScreen(incoming_message_.select_source().screen().id());
    }
    else if (incoming_message_.has_set_features())
    {
        const proto::internal::SetFeatures& set_features = incoming_message_.set_features();

        if (screen_capturer_)
        {
            screen_capturer_->enableWallpaper(set_features.wallpaper());
            screen_capturer_->enableEffects(set_features.effects());
        }
    }
    else if (incoming_message_.has_set_block_input())
    {
        if (input_injector_)
            input_injector_->setBlockInput(incoming_message_.set_block_input().state());
    }
    else if (incoming_message_.has_logoff_user_session())
    {
        base::PowerController::logoff();
    }
    else if (incoming_message_.has_lock_user_session())
    {
        base::PowerController::lock();
    }
    else
    {
        LOG(LS_ERROR) << "Unhandled message from service";
        return;
    }
}

void DesktopSessionAgent::onSharedMemoryCreate(int id)
{
    outgoing_message_.Clear();
    outgoing_message_.mutable_create_shared_buffer()->set_shared_buffer_id(id);
    channel_->send(common::serializeMessage(outgoing_message_));
}

void DesktopSessionAgent::onSharedMemoryDestroy(int id)
{
    outgoing_message_.Clear();
    outgoing_message_.mutable_release_shared_buffer()->set_shared_buffer_id(id);
    channel_->send(common::serializeMessage(outgoing_message_));
}

void DesktopSessionAgent::onScreenListChanged(
    const desktop::ScreenCapturer::ScreenList& list, desktop::ScreenCapturer::ScreenId current)
{
    outgoing_message_.Clear();

    proto::ScreenList* screen_list = outgoing_message_.mutable_screen_list();
    screen_list->set_current_screen(current);

    for (const auto& list_item : list)
    {
        proto::Screen* screen = screen_list->add_screen();
        screen->set_id(list_item.id);
        screen->set_title(list_item.title);
    }

    channel_->send(common::serializeMessage(outgoing_message_));
}

void DesktopSessionAgent::onScreenCaptured(std::unique_ptr<desktop::SharedFrame> frame)
{
    if (frame->constUpdatedRegion().isEmpty())
    {
        captureEnd();
        return;
    }

    outgoing_message_.Clear();

    proto::internal::SerializedDesktopFrame* serialized_frame =
        outgoing_message_.mutable_encode_frame()->mutable_frame();

    serialized_frame->set_shared_buffer_id(frame->sharedMemory()->id());

    desktop::Rect frame_rect = desktop::Rect::makeXYWH(frame->topLeft(), frame->size());

    codec::serializeRect(frame_rect, serialized_frame->mutable_desktop_rect());
    codec::serializePixelFormat(frame->format(), serialized_frame->mutable_pixel_format());

    for (desktop::Region::Iterator it(frame->constUpdatedRegion()); !it.isAtEnd(); it.advance())
        codec::serializeRect(it.rect(), serialized_frame->add_dirty_rect());

    last_frame_ = std::move(frame);
    channel_->send(common::serializeMessage(outgoing_message_));
}

void DesktopSessionAgent::onClipboardEvent(const proto::ClipboardEvent& event)
{
    outgoing_message_.Clear();
    outgoing_message_.mutable_clipboard_event()->CopyFrom(event);
    channel_->send(common::serializeMessage(outgoing_message_));
}

void DesktopSessionAgent::startSession()
{
    if (input_injector_)
    {
        LOG(LS_INFO) << "Session already started";
        return;
    }

    LOG(LS_INFO) << "Session start...";

    input_injector_ = std::make_unique<InputInjectorWin>();

    // A window is created to monitor the clipboard. We cannot create windows in the current
    // thread. Create a separate thread.
    clipboard_monitor_ = std::make_unique<ClipboardMonitor>();
    clipboard_monitor_->start(task_runner_, this);

    // Create a shared memory factory.
    // We will receive notifications of all creations and destruction of shared memory.
    shared_memory_factory_ = std::make_unique<ipc::SharedMemoryFactory>(this);

    capture_scheduler_ = std::make_unique<desktop::CaptureScheduler>(
        std::chrono::milliseconds(33));

    screen_capturer_ = std::make_unique<desktop::ScreenCapturerWrapper>(this);
    screen_capturer_->setSharedMemoryFactory(shared_memory_factory_.get());

    LOG(LS_INFO) << "Session successfully started";

    task_runner_->postTask(std::bind(&DesktopSessionAgent::captureBegin, shared_from_this()));
}

void DesktopSessionAgent::stopSession()
{
    LOG(LS_INFO) << "Session stop...";

    input_injector_.reset();
    capture_scheduler_.reset();
    screen_capturer_.reset();
    shared_memory_factory_.reset();
    clipboard_monitor_.reset();

    LOG(LS_INFO) << "Session successfully stopped";
}

void DesktopSessionAgent::captureBegin()
{
    if (!capture_scheduler_ || !screen_capturer_)
        return;

    capture_scheduler_->beginCapture();
    screen_capturer_->captureFrame();
}

void DesktopSessionAgent::captureEnd()
{
    if (!capture_scheduler_)
        return;

    capture_scheduler_->endCapture();

    task_runner_->postDelayedTask(
        std::bind(&DesktopSessionAgent::captureBegin, shared_from_this()),
        capture_scheduler_->nextCaptureDelay());
}

} // namespace host
