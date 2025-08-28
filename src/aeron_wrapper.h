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
#include "Aeron.h"
#include "concurrent/AtomicBuffer.h"
#include "concurrent/ringbuffer/OneToOneRingBuffer.h"

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
std::string pubresult_to_string(PublicationResult pubResult) noexcept;

// Exception classes
class AeronException : public std::runtime_error {
   public:
    explicit AeronException(const std::string& message) noexcept;
};

// Forward declarations
class Publication;
class Subscription;
class Aeron;

// Fragment handler with metadata
struct FragmentData final {
    aeron::concurrent::AtomicBuffer atomicBuffer;
    aeron::util::index_t length;
    aeron::util::index_t offset;
    aeron::Header header;

    // Helper to get data as string
    std::string as_string() const noexcept;

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
                const ConnectionHandler& connectionHandler = nullptr) noexcept;

   public:
    ~Publication() noexcept = default;

    // Non-copyable but movable
    Publication(const Publication&) = delete;
    Publication& operator=(const Publication&) = delete;
    Publication(Publication&&) = default;
    Publication& operator=(Publication&&) = default;

    // Publishing methods with better error handling
    PublicationResult offer(const std::uint8_t* buffer,
                            std::size_t length) noexcept;

    PublicationResult offer(const std::string& message) noexcept;

    template <typename T>
    PublicationResult offer(const T& data) noexcept;

    // Offer with retry logic
    PublicationResult offer_with_retry(
        const std::uint8_t* buffer, std::size_t length, int maxRetries = 3,
        std::chrono::milliseconds retryDelay =
            std::chrono::milliseconds(1)) noexcept;

    PublicationResult offer_with_retry(const std::string& message,
                                       int maxRetries = 3) noexcept;

    // Synchronous publish (blocks until success or failure)
    bool publish_sync(const std::uint8_t* buffer, std::size_t length,
                      std::chrono::milliseconds timeout =
                          std::chrono::milliseconds(5000)) noexcept;

    bool publish_sync(const std::string& message,
                      std::chrono::milliseconds timeout =
                          std::chrono::milliseconds(5000)) noexcept;

    // Status methods
    bool is_connected() const noexcept;

    bool is_closed() const noexcept;

    std::int64_t position() const noexcept;

    std::int32_t session_id() const noexcept;

    std::int32_t stream_id() const noexcept;

    const std::string& channel() const noexcept;

   private:
    void check_connection_state() noexcept;
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
                 const ConnectionHandler& connectionHandler = nullptr) noexcept;

   public:
    ~Subscription() noexcept = default;

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
                         const FragmentHandler& fragmentHandler) noexcept;

        ~BackgroundPoller() noexcept;

        // Non-copyable, non-movable
        BackgroundPoller(const BackgroundPoller&) = delete;
        BackgroundPoller& operator=(const BackgroundPoller&) = delete;
        BackgroundPoller(BackgroundPoller&&) = delete;
        BackgroundPoller& operator=(BackgroundPoller&&) = delete;

        void stop() noexcept;

        bool is_running() const noexcept;
    };

    // Polling methods
    int poll(const FragmentHandler& fragmentHandler,
             int fragmentLimit = 10) noexcept;

    // Block poll - polls until at least one message or timeout
    int block_poll(
        const FragmentHandler& fragmentHandler,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1000),
        int fragmentLimit = 10) noexcept;

    std::unique_ptr<BackgroundPoller> start_background_polling(
        const FragmentHandler& fragmentHandler) noexcept;

    // handler to be used in poll
    aeron::fragment_handler_t fragment_handler(
        const FragmentHandler& fragmentHandler) noexcept;

    // Status methods
    bool is_connected() const noexcept;

    bool is_closed() const noexcept;

    bool has_images() const noexcept;

    std::int32_t stream_id() const noexcept;

    const std::string& channel() const noexcept;

    std::size_t image_count() const noexcept;

   private:
    void check_connection_state() noexcept;
};

class RingBuffer final {
   public:
    static constexpr auto TRAILER_LENGTH =
        aeron::concurrent::ringbuffer::RingBufferDescriptor::TRAILER_LENGTH;

    RingBuffer(size_t size) noexcept;

    bool write_buffer(const aeron_wrapper::FragmentData& fragmentData) noexcept;

    void read_buffer(ReadHandler readHandler) noexcept;

    ~RingBuffer() = default;

   private:
    std::vector<uint8_t> _buffer;
    aeron::concurrent::AtomicBuffer _atomicBuffer;
    aeron::concurrent::ringbuffer::OneToOneRingBuffer _ringBuffer;
};

// RAII wrapper for Aeron Client
class Aeron final {
   private:
    std::shared_ptr<aeron::Aeron> _aeron;
    std::atomic<bool> _isRunning{false};

   public:
    // Constructor with optional context configuration
    explicit Aeron(const std::string& aeronDir = "") noexcept;

    ~Aeron() noexcept;

    // Non-copyable
    Aeron(const Aeron&) = delete;
    Aeron& operator=(const Aeron&) = delete;

    // Movable
    Aeron(Aeron&& aeron) noexcept;
    Aeron& operator=(Aeron&& aeron) noexcept;

    void close() noexcept;

    bool is_running() const noexcept;

    std::shared_ptr<aeron::Aeron> aeron() const noexcept;

    // Factory methods
    std::unique_ptr<Publication> create_publication(
        const std::string& channel, std::int32_t streamId,
        const ConnectionHandler& connectionHandler = nullptr);

    std::unique_ptr<Subscription> create_subscription(
        const std::string& channel, std::int32_t streamId,
        const ConnectionHandler& connectionHandler = nullptr);
};

}  // namespace aeron_wrapper
