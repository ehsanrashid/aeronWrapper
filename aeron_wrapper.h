#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

// Aeron C++ headers
#include <concurrent/BackOffIdleStrategy.h>

#include "Aeron.h"
#include "concurrent/AgentRunner.h"
#include "concurrent/AtomicBuffer.h"
#include "concurrent/ringbuffer/OneToOneRingBuffer.h"
#include "util/Index.h"

constexpr auto TRAILER_LENGTH =
    aeron::concurrent::ringbuffer::RingBufferDescriptor::TRAILER_LENGTH;

namespace aeron_wrapper {

// Publication result enum for better error handling
enum class PublicationResult : int8_t {
    SUCCESS = 1,
    NOT_CONNECTED = -1,
    BACK_PRESSURED = -2,
    ADMIN_ACTION = -3,
    CLOSED = -4,
    MAX_POSITION_EXCEEDED = -5
};

// Get publication constants as string for debugging
std::string pubresult_to_string(PublicationResult pubResult);

// Exception classes
class AeronException : public std::runtime_error {
   public:
    explicit AeronException(const std::string& message);
};

// Forward declarations
class Publication;
class Subscription;
class Aeron;

// Fragment handler with metadata
struct FragmentData final {
    const aeron::concurrent::AtomicBuffer& atomicBuffer;
    aeron::util::index_t length;
    aeron::util::index_t offset;
    const aeron::Header& header;

    // Helper to get data as string
    std::string as_string() const;

    // Helper to get data as specific type
    template <typename T>
    const T& as() const;
};

using FragmentHandler = std::function<void(const FragmentData& fragment)>;
using ReadHandler =
    std::function<bool(int8_t, char*, int32_t, int32_t, int32_t)>;
// Connection state callback
using ConnectionHandler = std::function<void(bool connected)>;

// Publication wrapper with enhanced functionality
class Publication final {
   private:
    std::shared_ptr<aeron::Publication> _publication;
    std::string _channel;
    std::int32_t _streamId;
    ConnectionHandler _connectionHandler;
    std::atomic<bool> _wasConnected{false};

    friend class Aeron;

    Publication(std::shared_ptr<aeron::Publication> pub,
                const std::string& channel, std::int32_t streamId,
                const ConnectionHandler& connectionHandler = nullptr);

   public:
    ~Publication() = default;

    // Non-copyable but movable
    Publication(const Publication&) = delete;
    Publication& operator=(const Publication&) = delete;
    Publication(Publication&&) = default;
    Publication& operator=(Publication&&) = default;

    // Publishing methods with better error handling
    PublicationResult offer(const std::uint8_t* buffer, std::size_t length);

    PublicationResult offer(const std::string& message);

    template <typename T>
    PublicationResult offer(const T& data);

    // Offer with retry logic
    PublicationResult offer_with_retry(
        const std::uint8_t* buffer, std::size_t length, int maxRetries = 3,
        std::chrono::milliseconds retryDelay = std::chrono::milliseconds(1));

    PublicationResult offer_with_retry(const std::string& message,
                                       int maxRetries = 3);

    // Synchronous publish (blocks until success or failure)
    bool publish_sync(
        const std::uint8_t* buffer, std::size_t length,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    bool publish_sync(
        const std::string& message,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // Status methods
    bool is_connected() const;

    bool is_closed() const;

    std::int64_t position() const;

    std::int32_t session_id() const;

    std::int32_t stream_id() const;
    const std::string& channel() const;

   private:
    void check_connection_state();
};

// Subscription wrapper with enhanced functionality
class Subscription final {
   private:
    std::shared_ptr<aeron::Subscription> _subscription;
    std::string _channel;
    std::int32_t _streamId;
    ConnectionHandler _connectionHandler;
    std::atomic<bool> _wasConnected{false};

    friend class Aeron;

    Subscription(std::shared_ptr<aeron::Subscription> sub,
                 const std::string& channel, std::int32_t streamId,
                 const ConnectionHandler& connectionHandler = nullptr);

   public:
    ~Subscription() = default;

    // Non-copyable but movable
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&&) = default;
    Subscription& operator=(Subscription&&) = default;

    // Continuous polling in background thread
    class BackgroundPoller {
       private:
        std::unique_ptr<std::thread> _pollThread;
        std::atomic<bool> _isRunning{false};

       public:
        BackgroundPoller(Subscription* subscription,
                         const FragmentHandler& fragmentHandler);

        ~BackgroundPoller();

        // Non-copyable, non-movable
        BackgroundPoller(const BackgroundPoller&) = delete;
        BackgroundPoller& operator=(const BackgroundPoller&) = delete;
        BackgroundPoller(BackgroundPoller&&) = delete;
        BackgroundPoller& operator=(BackgroundPoller&&) = delete;

        void stop();

        bool is_running() const;
    };

    // Polling methods
    int poll(const FragmentHandler& fragmentHandler, int fragmentLimit = 10);

    // Block poll - polls until at least one message or timeout
    int block_poll(
        const FragmentHandler& fragmentHandler,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1000),
        int fragmentLimit = 10);

    std::unique_ptr<BackgroundPoller> start_background_polling(
        const FragmentHandler& fragmentHandler);

    // handler to be used in poll
    aeron::fragment_handler_t fragment_handler(
        const FragmentHandler& fragmentHandler);

    // Status methods
    bool is_connected() const;

    bool is_closed() const;

    bool has_images() const;

    std::int32_t stream_id() const;
    const std::string& channel() const;

    std::size_t image_count() const;

   private:
    void check_connection_state();
};

class RingBuffer final {
   public:
    RingBuffer(size_t size)
        : _buffer(size + TRAILER_LENGTH),
          _atomicBuffer(_buffer.data(), size + TRAILER_LENGTH),
          _ringBuffer(_atomicBuffer),
          _backoffIdleStrategy(100, 1000) {}

    bool write_buffer(const aeron_wrapper::FragmentData& fragmentData) {
        bool isWritten = false;
        auto start = std::chrono::high_resolution_clock::now();
        while (!isWritten) {
            isWritten =
                _ringBuffer.write(1,
                                  const_cast<aeron::concurrent::AtomicBuffer&>(
                                      fragmentData.atomicBuffer),
                                  fragmentData.offset, fragmentData.length);
            if (isWritten) break;
            if (std::chrono::high_resolution_clock::now() - start >=
                std::chrono::microseconds(50)) {
                std::cerr << "retry timeout" << std::endl;
                break;
            }
            _backoffIdleStrategy.idle();
        }
        return isWritten;
    }

    void read_buffer(ReadHandler readHandler) {
        _ringBuffer.read([&](int8_t msgType,
                             aeron::concurrent::AtomicBuffer& atomicBuffer,
                             int32_t offset, int32_t length) {
            return readHandler(msgType,
                               reinterpret_cast<char*>(atomicBuffer.buffer()),
                               offset, length, atomicBuffer.capacity());
        });
    }

    ~RingBuffer() = default;

   private:
    std::vector<uint8_t> _buffer;
    aeron::concurrent::AtomicBuffer _atomicBuffer;
    aeron::concurrent::ringbuffer::OneToOneRingBuffer _ringBuffer;
    aeron::concurrent::BackoffIdleStrategy _backoffIdleStrategy;
};

// RAII wrapper for Aeron Client
class Aeron final {
   private:
    std::shared_ptr<aeron::Aeron> _aeron;
    std::atomic<bool> _isRunning{false};

   public:
    // Constructor with optional context configuration
    explicit Aeron(const std::string& aeronDir = "");

    ~Aeron();

    // Non-copyable
    Aeron(const Aeron&) = delete;
    Aeron& operator=(const Aeron&) = delete;

    // Movable
    Aeron(Aeron&& aeron) noexcept;

    Aeron& operator=(Aeron&& aeron) noexcept;

    void close();

    bool is_running() const;

    std::shared_ptr<aeron::Aeron> get_aeron() const;

    // Factory methods
    std::unique_ptr<Publication> create_publication(
        const std::string& channel, std::int32_t streamId,
        const ConnectionHandler& connectionHandler = nullptr);

    std::unique_ptr<Subscription> create_subscription(
        const std::string& channel, std::int32_t streamId,
        const ConnectionHandler& connectionHandler = nullptr);
};

}  // namespace aeron_wrapper
