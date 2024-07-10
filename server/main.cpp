#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <iostream>
#include <set>
#include <deque>

using boost::asio::ip::tcp;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;

/**
 * @brief Interface for chat users.
 */
class Users {
    public:
        Users() = default;
        Users(const std::string& username) : username_(username) {}
        /**
         * @brief Send a message to users.
         * @param msg Message to send.
         */
        virtual void deliver(const std::string& msg) = 0;
        virtual ~Users() {}
    private:
        std::string username_;
};
/**
 * @brief Class for chat room.
 */
class ChatRoom {
    public:
        ChatRoom() = default;
        /**
         * @brief Add a user to the chat room.
         * @param new_user New user to add.
         */
        void join(std::shared_ptr<Users> new_user) {
            users_.insert(new_user);
            for (auto& message : recent_message_) {
                new_user->deliver(message);
            }
        }
        /**
         * @brief Remove a user from the chat room.
         * @param remove_user User to remove.
         */
        void leave(std::shared_ptr<Users> remove_user) {
            users_.erase(remove_user);
        }
        /**
         * @brief Deliver a message to all users.
         * @param message Message to deliver.
         */
        void deliver(const std::string& message) {
            recent_message_.emplace_back(message);
            
            // Keep only the last max_recent_ messages
            while (recent_message_.size() > max_recent_) {
                recent_message_.pop_front();
            }

            for (auto user : users_) {
                user->deliver(message);
            }
        }

    private:
        std::set<std::shared_ptr<Users>> users_;
        std::deque<std::string> recent_message_;
        const int max_recent_ = 10;
};
/**
 * @brief Chat session for a single user.
 */
class ChatSession : public Users, public std::enable_shared_from_this<ChatSession> {
    public:
        /**
         * @brief Constructor for chat session.
         * @param socket TCP socket.
         * @param room Chat room.
         */
        ChatSession(tcp::socket socket, ChatRoom& room, std::string username) :
            socket_(std::move(socket)), timer_(socket_.get_executor()), room_(room), username_(username) {
            timer_.expires_at(std::chrono::steady_clock::time_point::max());
        }
        /**
         * @brief Start the chat session.
         */
        void start() {
            room_.join(shared_from_this());
            deliver("Welcome to the chat, " + username_ + "!");
            co_spawn(socket_.get_executor(), [sft = shared_from_this()]{return sft->reader();}, detached);
            co_spawn(socket_.get_executor(), [sft = shared_from_this()]{return sft->writer();}, detached);
        }
         /**
         * @brief Cancel the current asynchronous operation.
         */
        void cancel() {
            boost::system::error_code ec;
            // Cancels one asynchronous operation that is waiting on the timer.
            std::size_t num_cancelled = timer_.cancel_one(ec);
            if (ec) {
                std::cout << "Cancel error: " << ec.message() << std::endl;
            } else {
                std::cout << "Number of cancelled operations: " << num_cancelled << std::endl;
            }
        }
        /**
         * @brief Deliver a message to this user.
         * @param message Message to deliver.
         */
        void deliver(const std::string& message) {
            write_message_.push_back(message);
            cancel();
        }
    private:
        /**
         * @brief Coroutine to read messages from the socket.
         * @return Awaitable<void>
         */
        awaitable<void> reader() {
            try {
                std::string read_message;
                while(true) {
                    size_t n = co_await boost::asio::async_read_until(socket_, boost::asio::dynamic_buffer(read_message, 1024), "\n", use_awaitable);
                    room_.deliver(read_message.substr(0, n));
                    read_message.erase(0, n);
                }
            } catch (boost::system::system_error& e) {
                std::cerr << "Async read error: " << e.what() << std::endl;
                stop();
            }catch (std::exception&) {
                stop();
            }
        }
        /**
         * @brief Coroutine to write messages to the socket.
         * @return Awaitable<void>
         */
        awaitable<void> writer() {
            try {
                while (socket_.is_open()) {
                   if (!write_message_.empty()) {
                        /*------co_await-------
                        Унарный оператор, позволяющий, в общем случае, приостановить выполнение
                        сопрограммы и передать управление вызывающей стороне, пока не завершатся
                        вычисления представленные операндом
                        */
                        co_await boost::asio::async_write(socket_, boost::asio::buffer(write_message_.front() + '\n'), use_awaitable);
                        write_message_.pop_front();
                   } else {
                        boost::system::error_code ec;
                        co_await timer_.async_wait(redirect_error(use_awaitable, ec));
                   }
                }       
            } catch (std::exception&) {
                stop();
            }
        }
        /**
         * @brief Stop the chat session.
         */
        void stop() {
            room_.leave(shared_from_this()); 
            socket_.close();
            timer_.cancel();
        }
        tcp::socket socket_;
        boost::asio::steady_timer timer_;
        ChatRoom& room_;
        std::deque<std::string> write_message_;
        std::string username_;
};
/**
 * @brief Listener coroutine to accept incoming connections.
 * @param acceptor TCP acceptor.
 * @return Awaitable<void>
 */
awaitable<void> listener(tcp::acceptor acceptor) {
    ChatRoom room;
    while (true) {
        tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
        boost::asio::streambuf buf;
        boost::system::error_code ec;
        size_t bytes_transferred = co_await boost::asio::async_read_until(socket, buf, "\n", use_awaitable);

        if (!ec) {
            std::istream is(&buf);
            std::string username;
            std::getline(is, username);
            std::make_shared<ChatSession>(std::move(socket), room, std::move(username))->start();
        } else {
            std::cerr << "Error reading username: " << ec.message() << std::endl;
            socket.close();
        }
    }
}
/**
 * @brief Main function.
 * @param argc Number of arguments.
 * @param argv User arguments(port).
 * @return int Exit code.
 */
int main(int cnt_paraments, char* ports[]) {
    try {

        if (cnt_paraments < 2) {
            std::cerr << "No port provided. Usage: ./chat_server <port1> ...";
        }
        boost::asio::io_context io_context(1);
        for (int i = 0; i < cnt_paraments; ++i) {
            unsigned short port = std::atoi(ports[i]);
            co_spawn(io_context, listener(tcp::acceptor(io_context, {tcp::v4(), port})), detached);
        }
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto){ io_context.stop(); });
        io_context.run();
    } catch (std::exception& err){
        std::cerr << err.what() << '\n';
    }
    return 0;
}