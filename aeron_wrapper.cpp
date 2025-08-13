#include "aeron_wrapper.h"
#include "FragmentAssembler.h"

namespace aeron_wrapper {

static constexpr std::chrono::duration<long, std::milli> SLEEP_IDLE_MS(1);
// Get publication constants as string for debugging
std::string pubresult_to_string(PublicationResult pubResult) {
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

AeronException::AeronException(const std::string& message)
    : std::runtime_error("AeronWrapper: " + message) {}

// Helper to get data as string
std::string FragmentData::as_string() const {
    return std::string(reinterpret_cast<const char*>(buffer.buffer()), length);
}

// Helper to get data as specific type
template <typename T>
const T& FragmentData::as() const {
    if (length < sizeof(T)) {
        throw AeronException("Fragment too small for requested type");
    }
    return *reinterpret_cast<const T*>(buffer.buffer());
}

Publication::Publication(std::shared_ptr<aeron::Publication> pub,
                         const std::string& channel, std::int32_t streamId,
                         const ConnectionHandler& connectionHandler)
    : publication_(std::move(pub)),
      channel_(channel),
      streamId_(streamId),
      connectionHandler_(connectionHandler) {}

// Publishing methods with better error handling
PublicationResult Publication::offer(const std::uint8_t* buffer,
                                     std::size_t length) {
    check_connection_state();

    if (!publication_) {
        return PublicationResult::CLOSED;
    }

    // Create AtomicBuffer from raw pointer and length
    // Note: AtomicBuffer takes non-const pointer, but Aeron doesn't modify
    // during offer
    aeron::concurrent::AtomicBuffer atomicBuffer(
        const_cast<std::uint8_t*>(buffer),
        static_cast<aeron::util::index_t>(length));

    std::int64_t result = publication_->offer(
        atomicBuffer, 0, static_cast<aeron::util::index_t>(length));

    // Positive values indicate success (number of bytes written)
    if (result > 0) {
        return PublicationResult::SUCCESS;
    }
    // Handle negative error codes
    return static_cast<PublicationResult>(result);
}

PublicationResult Publication::offer(const std::string& message) {
    return offer(reinterpret_cast<const std::uint8_t*>(message.data()),
                 message.size());
}

template <typename T>
PublicationResult Publication::offer(const T& data) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable");
    return offer(reinterpret_cast<const std::uint8_t*>(&data), sizeof(T));
}

// Offer with retry logic
PublicationResult Publication::offer_with_retry(
    const std::uint8_t* buffer, std::size_t length, int maxRetries,
    std::chrono::milliseconds retryDelay) {
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
                                                int maxRetries) {
    return offer_with_retry(
        reinterpret_cast<const std::uint8_t*>(message.data()), message.size(),
        maxRetries);
}

// Synchronous publish (blocks until success or failure)
bool Publication::publish_sync(const std::uint8_t* buffer, std::size_t length,
                               std::chrono::milliseconds timeout) {
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
                               std::chrono::milliseconds timeout) {
    return publish_sync(reinterpret_cast<const std::uint8_t*>(message.data()),
                        message.size(), timeout);
}

// Status methods
bool Publication::is_connected() const {
    return publication_ && publication_->isConnected();
}

bool Publication::is_closed() const {
    return !publication_ || publication_->isClosed();
}

std::int64_t Publication::position() const {
    return publication_ ? publication_->position() : -1;
}

std::int32_t Publication::session_id() const {
    return publication_ ? publication_->sessionId() : -1;
}

std::int32_t Publication::stream_id() const { return streamId_; }
const std::string& Publication::channel() const { return channel_; }

void Publication::check_connection_state() {
    if (connectionHandler_) {
        bool isConnected = is_connected();
        bool wasConnected = wasConnected_.exchange(isConnected);

        if (isConnected != wasConnected) {
            connectionHandler_(isConnected);
        }
    }
}

Subscription::BackgroundPoller::BackgroundPoller(
    Subscription* subscription, const FragmentHandler& fragmentHandler) {
    isRunning_ = true;
    pollThread_ = std::make_unique<std::thread>([this, subscription,
                                                 fragmentHandler]() {
        aeron::SleepingIdleStrategy sleepStrategy(SLEEP_IDLE_MS);
        while (isRunning_) {
            try {
                int fragments = subscription->poll(fragmentHandler, 10);
                sleepStrategy.idle(fragments);
            } catch (const std::exception&) {
                // Log error in real implementation
                break;
            }
        }
    });
}

Subscription::BackgroundPoller::~BackgroundPoller() { stop(); }

void Subscription::BackgroundPoller::stop() {
    if (isRunning_) {
        isRunning_ = false;
        if (pollThread_ && pollThread_->joinable()) {
            pollThread_->join();
        }
    }
}

bool Subscription::BackgroundPoller::is_running() const { return isRunning_; }

Subscription::Subscription(std::shared_ptr<aeron::Subscription> sub,
                           const std::string& channel, std::int32_t streamId,
                           const ConnectionHandler& connectionHandler)
    : subscription_(std::move(sub)),
      channel_(channel),
      streamId_(streamId),
      connectionHandler_(connectionHandler) {}

// Polling methods
int Subscription::poll(const FragmentHandler& fragmentHandler,
                       int fragmentLimit) {
    check_connection_state();

    if (!subscription_) {
        return 0;
    }
    aeron::FragmentAssembler fragmentAssembler(fragHandler(fragmentHandler));
    aeron::fragment_handler_t handler = fragmentAssembler.handler();
    return subscription_->poll(handler, fragmentLimit);
}

// Block poll - polls until at least one message or timeout
int Subscription::block_poll(const FragmentHandler& fragmentHandler,
                             std::chrono::milliseconds timeout,
                             int fragmentLimit) {
    auto start = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start < timeout) {
        int fragments = poll(fragmentHandler, fragmentLimit);
        if (fragments > 0) {
            return fragments;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    return 0;
}

std::unique_ptr<Subscription::BackgroundPoller>
Subscription::start_background_polling(const FragmentHandler& fragmentHandler) {
    return std::make_unique<BackgroundPoller>(this, fragmentHandler);
}

// Status methods
bool Subscription::is_connected() const {
    return subscription_ && subscription_->isConnected();
}

bool Subscription::is_closed() const {
    return !subscription_ || subscription_->isClosed();
}

bool Subscription::has_images() const {
    return subscription_ && subscription_->imageCount() > 0;
}

std::int32_t Subscription::stream_id() const { return streamId_; }
const std::string& Subscription::channel() const { return channel_; }

std::size_t Subscription::image_count() const {
    return subscription_ ? subscription_->imageCount() : 0;
}

void Subscription::check_connection_state() {
    if (connectionHandler_) {
        bool isConnected = is_connected();
        bool wasConnected = wasConnected_.exchange(isConnected);

        if (isConnected != wasConnected) {
            connectionHandler_(isConnected);
        }
    }
}

// Constructor with optional context configuration
Aeron::Aeron(const std::string& aeronDir) {
    aeron::Context context;

    if (!aeronDir.empty()) {
        context.aeronDir(aeronDir);
    }

    try {
        aeron_ = aeron::Aeron::connect(context);
        isRunning_ = true;
    } catch (const std::exception& e) {
        throw AeronException("Failed to connect to Aeron: " +
                             std::string(e.what()));
    }
}

Aeron::~Aeron() { close(); }

// Movable
Aeron::Aeron(Aeron&& aeron) noexcept
    : aeron_(std::move(aeron.aeron_)), isRunning_(aeron.isRunning_.load()) {
    aeron.isRunning_ = false;
}

Aeron& Aeron::operator=(Aeron&& aeron) noexcept {
    if (this != &aeron) {
        close();
        aeron_ = std::move(aeron.aeron_);
        isRunning_ = aeron.isRunning_.load();
        aeron.isRunning_ = false;
    }
    return *this;
}

void Aeron::close() {
    if (isRunning_) {
        isRunning_ = false;
        // Close publications and subscriptions handled by Aeron's RAII
        aeron_.reset();
    }
}

bool Aeron::is_running() const { return isRunning_; }

std::shared_ptr<aeron::Aeron> Aeron::get_aeron() const { return aeron_; }

// Implementation of Aeron factory methods

// Create publication
std::unique_ptr<Publication> Aeron::create_publication(
    const std::string& channel, std::int32_t streamId,
    const ConnectionHandler& connectionHandler) {
    if (!isRunning_) {
        throw AeronException("Aeron is not running");
    }

    try {
        auto publicationId = aeron_->addPublication(channel, streamId);

        // Poll for publication to become available
        std::shared_ptr<aeron::Publication> publication;
        auto timeout =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (std::chrono::steady_clock::now() < timeout) {
            publication = aeron_->findPublication(publicationId);
            if (publication) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (!publication) {
            throw AeronException("Failed to find publication with ID: " +
                                 std::to_string(publicationId));
        }

        // Wait for publication to be ready (with timeout)
        while (!publication->isConnected() && !publication->isClosed() &&
               std::chrono::steady_clock::now() < timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return std::unique_ptr<Publication>(new Publication(
            std::move(publication), channel, streamId, connectionHandler));

    } catch (const std::exception& e) {
        throw AeronException("Failed to create publication: " +
                             std::string(e.what()));
    }
}

// Create subscription
std::unique_ptr<Subscription> Aeron::create_subscription(
    const std::string& channel, std::int32_t streamId,
    const ConnectionHandler& connectionHandler) {
    if (!isRunning_) {
        throw AeronException("Aeron is not running");
    }

    try {
        auto subscriptionId = aeron_->addSubscription(channel, streamId);

        // Poll for subscription to become available
        std::shared_ptr<aeron::Subscription> subscription;
        auto timeout =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (std::chrono::steady_clock::now() < timeout) {
            subscription = aeron_->findSubscription(subscriptionId);
            if (subscription) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (!subscription) {
            throw AeronException("Failed to find subscription with ID: " +
                                 std::to_string(subscriptionId));
        }

        // Wait for subscription to be ready (with timeout)
        while (!subscription->isConnected() && !subscription->isClosed() &&
               std::chrono::steady_clock::now() < timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return std::unique_ptr<Subscription>(new Subscription(
            std::move(subscription), channel, streamId, connectionHandler));

    } catch (const std::exception& e) {
        throw AeronException("Failed to create subscription: " +
                             std::string(e.what()));
    }
}

aeron::fragment_handler_t Subscription::fragHandler(const FragmentHandler& fragmentHandler){
    return  [&](const aeron::AtomicBuffer &buffer,
               std::int32_t offset,
               std::int32_t length,
               const aeron::Header &header)
    {
        FragmentData fragmentData{
            buffer,
            offset,
            length,
            header
        };
        fragmentHandler(fragmentData);
    };
}

}  // namespace aeron_wrapper
