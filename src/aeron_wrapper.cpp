#include "aeron_wrapper.h"

#include "Context.h"
#include "FragmentAssembler.h"
#include "concurrent/BackOffIdleStrategy.h"
#include "concurrent/SleepingIdleStrategy.h"

namespace aeron_wrapper {

namespace {

template <typename Predicate>
bool wait_until(Predicate&& pred, std::chrono::microseconds interval,
                std::chrono::steady_clock::time_point deadline) noexcept {
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(interval);
    }
    return false;
}

}  // namespace

// Get publication constants as string for debugging
std::string pubresult_to_string(PublicationResult pubResult) noexcept {
    switch (pubResult) {
        case PublicationResult::SUCCESS:
            return "SUCCESS";
        case PublicationResult::NOT_CONNECTED:
            return "NOT_CONNECTED";
        case PublicationResult::BACK_PRESSURED:
            return "BACK_PRESSURED";
        case PublicationResult::ADMIN_ACTION:
            return "ADMIN_ACTION";
        case PublicationResult::CLOSED:
            return "CLOSED";
        case PublicationResult::MAX_POSITION_EXCEEDED:
            return "MAX_POSITION_EXCEEDED";
        default:
            return "UNKNOWN";
    }
}

AeronError::AeronError(const std::string& msg)
    : std::runtime_error("AeronWrapper: " + msg) {}

// Helper to get data as string
std::string FragmentData::as_string() const noexcept {
    return std::string(reinterpret_cast<const char*>(atomicBuffer.buffer()),
                       length);
}

// Helper to get data as specific type
template <typename T>
const T& FragmentData::as() const {
    if (length < sizeof(T)) {
        throw AeronError("Fragment too small for requested type");
    }
    return *reinterpret_cast<const T*>(atomicBuffer.buffer());
}

Publication::Publication(std::shared_ptr<aeron::Publication> publication,
                         std::string_view channel, std::int32_t streamId,
                         ConnectionHandler connectionHandler) noexcept
    : _publication(std::move(publication)),
      _channel(channel),
      _streamId(streamId),
      _connectionHandler(std::move(connectionHandler)),
      _wasConnected(false) {}

// Publishing methods with better error handling
PublicationResult Publication::offer(const std::uint8_t* buffer,
                                     std::size_t length) noexcept {
    check_connection_state();

    if (!_publication) {
        return PublicationResult::CLOSED;
    }

    // Create AtomicBuffer from raw pointer and length
    // Note: AtomicBuffer takes non-const pointer, but Aeron doesn't modify
    // during offer
    aeron::concurrent::AtomicBuffer atomicBuffer(
        const_cast<std::uint8_t*>(buffer), length);

    auto result = _publication->offer(
        atomicBuffer, 0, static_cast<aeron::util::index_t>(length));

    // Positive values indicate success (number of bytes written)
    if (result > 0) return PublicationResult::SUCCESS;
    // Handle negative error codes
    return static_cast<PublicationResult>(result);
}

PublicationResult Publication::offer(const std::string& message) noexcept {
    return offer(reinterpret_cast<const std::uint8_t*>(message.data()),
                 message.size());
}

template <typename T>
PublicationResult Publication::offer(const T& data) noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable");
    return offer(reinterpret_cast<const std::uint8_t*>(&data), sizeof(T));
}

// Offer with retry logic
PublicationResult Publication::offer_with_retry(
    const std::uint8_t* buffer, std::size_t length, int maxRetries,
    std::chrono::milliseconds retryDelay) noexcept {
    for (int i = 0; i <= maxRetries; ++i) {
        auto result = offer(buffer, length);

        if (result == PublicationResult::SUCCESS ||
            result == PublicationResult::CLOSED ||
            result == PublicationResult::NOT_CONNECTED ||
            result == PublicationResult::MAX_POSITION_EXCEEDED) {
            return result;
        }

        if (i < maxRetries) {
            std::this_thread::sleep_for(retryDelay);
        }
    }
    return PublicationResult::BACK_PRESSURED;
}

PublicationResult Publication::offer_with_retry(const std::string& message,
                                                int maxRetries) noexcept {
    return offer_with_retry(
        reinterpret_cast<const std::uint8_t*>(message.data()), message.size(),
        maxRetries);
}

// Synchronous publish (blocks until success or failure)
bool Publication::publish_sync(const std::uint8_t* buffer, std::size_t length,
                               std::chrono::milliseconds timeout) noexcept {
    auto start = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start < timeout) {
        auto result = offer(buffer, length);

        switch (result) {
            case PublicationResult::SUCCESS:
                return true;
            case PublicationResult::CLOSED:
            case PublicationResult::NOT_CONNECTED:
            case PublicationResult::MAX_POSITION_EXCEEDED:
                return false;
            case PublicationResult::BACK_PRESSURED:
            case PublicationResult::ADMIN_ACTION:
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                break;
        }
    }
    return false;
}

bool Publication::publish_sync(const std::string& message,
                               std::chrono::milliseconds timeout) noexcept {
    return publish_sync(reinterpret_cast<const std::uint8_t*>(message.data()),
                        message.size(), timeout);
}

// Status methods
bool Publication::is_connected() const noexcept {
    return _publication && _publication->isConnected();
}

bool Publication::is_closed() const noexcept {
    return !_publication || _publication->isClosed();
}

std::int64_t Publication::position() const noexcept {
    return _publication ? _publication->position() : -1;
}

std::int32_t Publication::session_id() const noexcept {
    return _publication ? _publication->sessionId() : -1;
}

std::int32_t Publication::stream_id() const noexcept { return _streamId; }

std::string_view Publication::channel() const noexcept { return _channel; }

void Publication::check_connection_state() noexcept {
    if (_connectionHandler) {
        bool isConnected = is_connected();
        bool wasConnected = _wasConnected.exchange(isConnected);

        if (isConnected != wasConnected) {
            _connectionHandler(isConnected);
        }
    }
}

Subscription::BackgroundPoller::BackgroundPoller(
    Subscription* subscription, FragmentHandler fragmentHandler) noexcept
    : _isRunning(true),
      _pollThread(std::make_unique<std::thread>([this, subscription,
                                                 fragmentHandler = std::move(
                                                     fragmentHandler)] {
          static aeron::concurrent::SleepingIdleStrategy sleepingIdleStrategy(
              std::chrono::milliseconds(1));

          while (_isRunning) {
              try {
                  int fragments = subscription->poll(fragmentHandler, 10);
                  sleepingIdleStrategy.idle(fragments);
              } catch (const std::exception&) {
                  // TODO: Log error in real implementation
                  _isRunning = false;
              }
          }
      })) {}

Subscription::BackgroundPoller::~BackgroundPoller() noexcept { stop(); }

void Subscription::BackgroundPoller::stop() noexcept {
    if (_isRunning) {
        _isRunning = false;
        if (_pollThread && _pollThread->joinable()) {
            _pollThread->join();
        }
    }
}

bool Subscription::BackgroundPoller::is_running() const noexcept {
    return _isRunning;
}

Subscription::Subscription(std::shared_ptr<aeron::Subscription> subscription,
                           std::string_view channel, std::int32_t streamId,
                           ConnectionHandler connectionHandler) noexcept
    : _subscription(std::move(subscription)),
      _channel(channel),
      _streamId(streamId),
      _connectionHandler(std::move(connectionHandler)),
      _wasConnected(false) {}

// Polling methods
int Subscription::poll(FragmentHandler fragmentHandler,
                       int fragmentLimit) noexcept {
    check_connection_state();

    if (!_subscription) return 0;

    aeron::FragmentAssembler fragmentAssembler(
        fragment_handler(std::move(fragmentHandler)));
    return _subscription->poll(fragmentAssembler.handler(), fragmentLimit);
}

// Block poll - polls until at least one message or timeout
int Subscription::block_poll(FragmentHandler fragmentHandler,
                             std::chrono::milliseconds timeout,
                             int fragmentLimit) noexcept {
    int fragments = 0;

    auto deadline = std::chrono::steady_clock::now() + timeout;
    wait_until(
        [&] {
            fragments = poll(std::move(fragmentHandler), fragmentLimit);
            return fragments > 0;
        },
        std::chrono::microseconds(1), deadline);

    return fragments;
}

std::unique_ptr<Subscription::BackgroundPoller>
Subscription::start_background_polling(
    FragmentHandler fragmentHandler) noexcept {
    return std::make_unique<BackgroundPoller>(this, std::move(fragmentHandler));
}

aeron::fragment_handler_t Subscription::fragment_handler(
    FragmentHandler fragmentHandler) noexcept {
    return [fragmentHandler = std::move(fragmentHandler)](
               const aeron::concurrent::AtomicBuffer& atomicBuffer,
               std::int32_t offset, std::int32_t length,
               const aeron::concurrent::logbuffer::Header& header) {
        fragmentHandler({atomicBuffer, length, offset, header});
    };
}

// Status methods
bool Subscription::is_connected() const noexcept {
    return _subscription && _subscription->isConnected();
}

bool Subscription::is_closed() const noexcept {
    return !_subscription || _subscription->isClosed();
}

bool Subscription::has_images() const noexcept {
    return _subscription && _subscription->imageCount() > 0;
}

std::int32_t Subscription::stream_id() const noexcept { return _streamId; }

std::string_view Subscription::channel() const noexcept { return _channel; }

std::size_t Subscription::image_count() const noexcept {
    return _subscription ? _subscription->imageCount() : 0;
}

void Subscription::check_connection_state() noexcept {
    if (_connectionHandler) {
        bool isConnected = is_connected();
        bool wasConnected = _wasConnected.exchange(isConnected);

        if (isConnected != wasConnected) {
            _connectionHandler(isConnected);
        }
    }
}

RingBuffer::RingBuffer(std::size_t size) noexcept
    : _buffer(size + TRAILER_LENGTH),
      _atomicBuffer(_buffer.data(), size + TRAILER_LENGTH),
      _ringBuffer(_atomicBuffer) {}

bool RingBuffer::write_buffer(const FragmentData& fragmentData) noexcept {
    static aeron::concurrent::BackoffIdleStrategy backoffIdleStrategy(100,
                                                                      1000);
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
        backoffIdleStrategy.idle();
    }
    return isWritten;
}

void RingBuffer::read_buffer(ReadHandler readHandler) noexcept {
    _ringBuffer.read([readHandler = std::move(readHandler)](
                         std::int8_t msgType,
                         aeron::concurrent::AtomicBuffer& atomicBuffer,
                         std::int32_t offset, std::int32_t length) {
        return readHandler(msgType,
                           reinterpret_cast<char*>(atomicBuffer.buffer()),
                           offset, length, atomicBuffer.capacity());
    });
}

// Constructor with optional context configuration
Aeron::Aeron(const std::string& aeronDir) : _isRunning(false) {
    try {
        aeron::Context context;
        if (!aeronDir.empty()) context.aeronDir(aeronDir);

        _aeron = aeron::Aeron::connect(context);
        _isRunning = true;
    } catch (const std::exception& e) {
        throw AeronError("Failed to connect to Aeron: " +
                         std::string(e.what()));
    }
}

Aeron::~Aeron() noexcept { close(); }

// Move constructor
Aeron::Aeron(Aeron&& aeron) noexcept
    : _aeron(std::move(aeron._aeron)),
      _isRunning(aeron._isRunning.exchange(false)) {}

// Move assignment
Aeron& Aeron::operator=(Aeron&& aeron) noexcept {
    if (this != &aeron) {
        close();
        _aeron = std::move(aeron._aeron);
        _isRunning = aeron._isRunning.exchange(false);
    }
    return *this;
}

void Aeron::close() noexcept {
    if (_isRunning) {
        _isRunning = false;
        // Close publications and subscriptions handled by Aeron's RAII
        _aeron.reset();
    }
}

bool Aeron::is_running() const noexcept { return _isRunning; }

std::shared_ptr<aeron::Aeron> Aeron::aeron() const noexcept { return _aeron; }

// Implementation of Aeron factory methods

// Create publication
std::unique_ptr<Publication> Aeron::create_publication(
    std::string_view channel, std::int32_t streamId,
    ConnectionHandler connectionHandler) {
    if (!_isRunning) {
        throw AeronError("Aeron is not running");
    }

    try {
        std::shared_ptr<aeron::Publication> publication;

        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);

        // Poll for publication to become available
        auto publicationId =
            _aeron->addPublication(std::string(channel), streamId);
        if (!wait_until(
                [&] {
                    publication = _aeron->findPublication(publicationId);
                    return static_cast<bool>(publication);
                },
                std::chrono::milliseconds(1), deadline)) {
            throw AeronError("Failed to find publication with ID: " +
                             std::to_string(publicationId));
        }

        // Wait for publication to be ready (with timeout)
        if (!wait_until(
                [&] {
                    return publication->isConnected() ||
                           publication->isClosed();
                },
                std::chrono::milliseconds(1), deadline)) {
            throw AeronError("Publication not ready before timeout");
        }

        return std::make_unique<Publication>(std::move(publication),  //
                                             channel, streamId,
                                             std::move(connectionHandler));
    } catch (const std::exception& e) {
        throw AeronError("Failed to create publication: " +
                         std::string(e.what()));
    }
}

// Create subscription
std::unique_ptr<Subscription> Aeron::create_subscription(
    std::string_view channel, std::int32_t streamId,
    ConnectionHandler connectionHandler) {
    if (!_isRunning) {
        throw AeronError("Aeron is not running");
    }

    try {
        std::shared_ptr<aeron::Subscription> subscription;

        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);

        // Poll for subscription to become available
        auto subscriptionId =
            _aeron->addSubscription(std::string(channel), streamId);
        if (!wait_until(
                [&] {
                    subscription = _aeron->findSubscription(subscriptionId);
                    return static_cast<bool>(subscription);
                },
                std::chrono::milliseconds(1), deadline)) {
            throw AeronError("Failed to find subscription with ID: " +
                             std::to_string(subscriptionId));
        }

        // Wait for subscription to be ready (with timeout)
        if (!wait_until(
                [&] {
                    return subscription->isConnected() ||
                           subscription->isClosed();
                },
                std::chrono::milliseconds(1), deadline)) {
            throw AeronError("Subscription not ready before timeout");
        }

        return std::make_unique<Subscription>(std::move(subscription),  //
                                              channel, streamId,
                                              std::move(connectionHandler));
    } catch (const std::exception& e) {
        throw AeronError("Failed to create subscription: " +
                         std::string(e.what()));
    }
}

}  // namespace aeron_wrapper
