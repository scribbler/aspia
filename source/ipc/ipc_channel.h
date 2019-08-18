//
// Aspia Project
// Copyright (C) 2019 Dmitry Chapyshev <dmitry@aspia.ru>
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

#ifndef IPC__IPC_CHANNEL_H
#define IPC__IPC_CHANNEL_H

#include "base/byte_array.h"
#include "base/macros_magic.h"
#include "base/process_handle.h"
#include "base/win/session_id.h"

#include <asio/windows/stream_handle.hpp>

#include <mutex>
#include <queue>

namespace ipc {

class ChannelProxy;
class Listener;
class Server;

class Channel
{
public:
    explicit Channel(asio::io_context& io_context);
    ~Channel();

    std::shared_ptr<ChannelProxy> channelProxy();

    // Sets an instance of the class to receive connection status notifications or new messages.
    // You can change this in the process.
    void setListener(Listener* listener);

    [[nodiscard]]
    bool connect(std::u16string_view channel_id);

    void disconnect();

    bool isConnected() const;
    bool isPaused() const;

    void pause();
    void resume();

    void send(base::ByteArray&& buffer);

    base::ProcessId peerProcessId() const { return peer_process_id_; }
    base::win::SessionId peerSessionId() const { return peer_session_id_; }

private:
    friend class Server;

    Channel(asio::io_context& io_context, asio::windows::stream_handle&& stream);
    static std::u16string channelName(std::u16string_view channel_id);

    void onErrorOccurred(const std::error_code& error_code);
    bool reloadWriteQueue();
    void scheduleWrite();
    void doWrite();
    void doReadMessage();
    void onMessageReceived();

    asio::io_context& io_context_;
    asio::windows::stream_handle stream_;

    std::shared_ptr<ChannelProxy> proxy_;
    Listener* listener_ = nullptr;

    bool is_connected_ = false;
    bool is_paused_ = true;

#if defined(USE_TBB)
    using QueueAllocator = tbb::scalable_allocator<base::ByteArray>;
#else // defined(USE_TBB)
    using QueueAllocator = std::allocator<base::ByteArray>;
#endif // defined(USE_*)

    using QueueContainer = std::deque<base::ByteArray, QueueAllocator>;

    class WriteQueue : public std::queue<base::ByteArray, QueueContainer>
    {
    public:
        void fastSwap(WriteQueue& queue)
        {
            // Calls std::deque::swap.
            c.swap(queue.c);
        }
    };

    WriteQueue incoming_write_queue_;
    std::mutex incoming_write_queue_lock_;

    WriteQueue work_write_queue_;
    uint32_t write_size_ = 0;

    uint32_t read_size_ = 0;
    base::ByteArray read_buffer_;

    base::ProcessId peer_process_id_ = base::kNullProcessId;
    base::win::SessionId peer_session_id_ = base::win::kInvalidSessionId;

    DISALLOW_COPY_AND_ASSIGN(Channel);
};

} // namespace ipc

#endif // IPC__IPC_CHANNEL_H
