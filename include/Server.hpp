/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: nlewicki <nlewicki@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:38 by mhummel           #+#    #+#             */
/*   Updated: 2025/11/21 11:46:45 by nlewicki         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef SERVER_HPP
# define SERVER_HPP

#include <string>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <sstream>

#include "HTTPHandler.hpp"
#include "Response.hpp"
#include "config.hpp"

enum class RxState { READING_HEADERS, READING_BODY, READY };

struct Client
{
    std::string rx; // Rohpuffer: während Header-Phase: Headerbytes; ab Body-Phase: Body/Reste
    std::string tx; // Antwort

    // Request-Empfang
    RxState state       = RxState::READING_HEADERS;
    bool header_done    = false;
    bool is_chunked     = false;
    size_t content_len  = 0;     // nur wenn Content-Length vorhanden
    size_t body_rcvd    = 0;     // gezählt (für CL und dechunk)

    // Limits (später aus Config)
    size_t max_header_bytes = 16 * 1024;       // 16KB
    size_t max_body_bytes   = 1 * 1024 * 1024; // 1MB

    // Timeout
    long last_active_ms = 0;

    std::string method, target, version;
    std::map<std::string,std::string> headers; // optional, später füllen
    bool keep_alive = false;

    // Chunked-Decoder-Context
    enum class ChunkState { SIZE, DATA, CRLF_AFTER_DATA, DONE };
    ChunkState ch_state = ChunkState::SIZE;
    size_t     ch_need  = 0;   // noch zu lesende Bytes im DATA-State

    // ==== NEU: für Config-Routing ====
    int listen_port = 0;          // vom Listener übernommen
    size_t server_idx = 0;        // welcher Server-Block (wird ggf. nach Host-Header präzisiert)
    std::string host;             // aus "Host:" Header (ggf. mit :port, vorher strippen)
};

struct HeadInfo
{
    std::string method;
    std::string target;
    std::string version;
    bool keep_alive = false;
    bool is_chunked = false;
    size_t content_length = 0;
};

class Server
{
    public:
        int run(int argc, char* argv[]);

    private:
        void loadConfig(int argc, char* argv[]);
        void setupListeners();

        //poll stuff
        void handleTimeouts(long now_ms, long idle_ms);
        void handleListenerEvent(size_t index, long now_ms);
        void handleClientRead(size_t &index, long now_ms, char* buf);
        void handleClientWrite(size_t &index, long now_ms);
        void closeClient(size_t &index);
};

int webserv(int argc, char* argv[]);

#endif
