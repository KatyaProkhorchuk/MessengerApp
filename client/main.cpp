
#include <deque>
#include <array>
#include <thread>
#include <iostream>
#include <cstring>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <chrono>

using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
/**
 * @class Client
 * @brief A class representing a chat client.
 */
class Client : public std::enable_shared_from_this<Client>
{
public:
    /**
     * @brief Constructs a new Client object.
     * @param username The username of the client.
     * @param io_service The IO service to use.
     * @param endpoint_iterator The endpoint iterator for the server.
     */
    Client(const std::string& username,
            boost::asio::io_service& io_service,
            tcp::resolver::iterator endpoint_iterator) :
            service_(io_service), socket_(io_service), username_(username) {

        boost::asio::async_connect(socket_, endpoint_iterator, boost::bind(&Client::start, this, _1));
    }

    /**
     * @brief Closes the connection.
     */
    void close() {
            service_.post(boost::bind(&Client::closeSocket, this));
    }
    /**
     * @brief Sends a message to the server.
     * @param msg The message to send.
     */
    void write(const std::string& msg) {
        service_.post(boost::bind(&Client::writeSocket, this, msg));
    }

private:
     /**
     * @brief Starts the client after a successful connection.
     * @param error The error code.
     */
    void start(const boost::system::error_code& error) {
        if (!error) {
            boost::asio::async_write(socket_,
                                     boost::asio::buffer(username_ + '\n', username_.size() + 1),
                                     boost::bind(&Client::reader, this, _1));
        }
    }
    /**
     * @brief Handles reading from the server.
     * @param error The error code.
     */
    void reader(const boost::system::error_code& error)
    { 
        if (!error) {   
            std::cout << read_message_ << std::endl;
            read_message_.clear();
            boost::asio::async_read_until(socket_,
                                          boost::asio::dynamic_buffer(read_message_, 1024), "\n",
                                          boost::bind(&Client::reader, this, _1));
            return;
        }
        closeSocket();
        
    }

    /**
     * @brief Initiates an asynchronous write operation.
     */
    void async_write() {
        boost::asio::async_write(socket_,
                                     boost::asio::buffer("[" + username_ + "] " + write_message_.front() + '\n', write_message_.front().size() + 4 + username_.size()),
                                     boost::bind(&Client::writer, this, _1));
    }

    /**
     * @brief Handles completion of a write operation.
     * @param error The error code.
     */
    void writer(const boost::system::error_code& error) {
        if (!error) {
            write_message_.pop_front();
            if (!write_message_.empty())
            {
                async_write();
            }
        } else {
            closeSocket();
        }
    }
   /**
     * @brief Implements the actual message writing logic.
     * @param msg The message to write.
     */
    void writeSocket(const std::string& msg) {
        auto write_in_progress = !write_message_.empty();
        write_message_.push_back(msg);
        if (!write_in_progress) {
            async_write();
        }
    }
    /**
     * @brief Closes the socket connection.
     */
    void closeSocket() {
        socket_.close();
    }

    boost::asio::io_service& service_;
    tcp::socket socket_;
    std::string read_message_;
    std::deque<std::string> write_message_;
    std::string username_;
};
/**
 * @brief The main function.
 * @param argc The argument count.
 * @param argv The user parametrs(username, host, port).
 * @return int The exit status.
 */
int main(int argc, char* argv[])
{
    try
    {
        if (argc != 3)
        {
            std::cerr << "Usage: chat_client <username> <port>\n";
            return 1;
        }
        // connect to host
        boost::asio::io_service io_service;
        tcp::resolver resolver(io_service);
        tcp::resolver::query query("localhost", argv[2]);
        tcp::resolver::iterator iterator = resolver.resolve(query);
        
        Client client(argv[1], io_service, iterator);

        std::thread t(boost::bind(&boost::asio::io_service::run, &io_service));

        std::string msg;

        while (true)
        {
            msg.clear();
            std::getline(std::cin, msg);
            if (!msg.empty())
            {
                std::cin.clear();
                client.write(msg);
            }
            
        }
        client.close();
        t.join();
    } catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
