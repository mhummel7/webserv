/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CGIHandler.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: leokubler <leokubler@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:20 by mhummel           #+#    #+#             */
/*   Updated: 2025/12/17 11:05:54 by leokubler        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include "HTTPHandler.hpp"
#include "Response.hpp"
#include <string>
#include <map>

enum CGI_Error
{
	CGI_SUCCESS = 0,
    CGI_TIMEOUT,
    CGI_INTERNAL_ERROR,
    CGI_SCRIPT_ERROR,
    CGI_FORK_ERROR,
    CGI_PIPE_ERROR,
    CGI_EXEC_ERROR
};

struct CGIResult
{
    std::string output;
    int exit_status;
    CGI_Error error;
    
    CGIResult() : exit_status(0), error(CGI_SUCCESS) {}
};

class CGIHandler
{
public:
    CGIHandler();
    ~CGIHandler();

    // Führt das CGI-Script aus und gibt HTTP-Response zurück
    Response execute(const Request& req);
    Response executeWith(const Request& req, const std::string& execPath, const std::string& scriptFile);
    
private:
    // Hilfsfunktionen
    std::map<std::string, std::string> buildEnv(const Request& req, const std::string& scriptPath);
    CGIResult runCGI(const std::string& scriptPath, const std::map<std::string, std::string>& env, 
                    const std::string& body, size_t timeout_ms);
    
    // Helper für Fehlerbehandlung
    Response createErrorResponse(CGI_Error error, int script_exit_status = 0);
};

#endif
